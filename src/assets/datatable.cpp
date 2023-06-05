#include "pch.h"
#include "assets.h"
#include "public/table.h"

static const std::unordered_map<std::string, dtblcoltype_t> s_DataTableColumnMap =
{
    { "bool",   dtblcoltype_t::Bool },
    { "int",    dtblcoltype_t::Int },
    { "float",  dtblcoltype_t::Float },
    { "vector", dtblcoltype_t::Vector },
    { "string", dtblcoltype_t::String },
    { "asset",  dtblcoltype_t::Asset },
    { "assetnoprecache", dtblcoltype_t::AssetNoPrecache }
};

static const std::regex s_VectorStringRegex("<(.*),(.*),(.*)>");

// gets enum value from type string
// e.g. "string" to dtblcoltype::StringT
dtblcoltype_t GetDataTableTypeFromString(std::string sType)
{
    std::transform(sType.begin(), sType.end(), sType.begin(), ::tolower);

    for (const auto& [key, value] : s_DataTableColumnMap) // Iterate through unordered_map.
    {
        if (sType.compare(key) == 0) // Do they equal?
            return value;
    }

    return dtblcoltype_t::String;
}

// get required data size to store the specified data type
uint8_t DataTable_GetEntrySize(dtblcoltype_t type)
{
    switch (type)
    {
    case dtblcoltype_t::Bool:
    case dtblcoltype_t::Int:
    case dtblcoltype_t::Float:
        return sizeof(int32_t);
    case dtblcoltype_t::Vector:
        return sizeof(Vector3);
    case dtblcoltype_t::String:
    case dtblcoltype_t::Asset:
    case dtblcoltype_t::AssetNoPrecache:
        // string types get placed elsewhere and are referenced with a pointer
        return sizeof(PagePtr_t);
    }

    Error("tried to get entry size for an unknown dtbl column type. asserting...\n");
    assert(0);
    return 0; // should be unreachable
}

bool DataTable_IsStringType(dtblcoltype_t type)
{
    switch (type)
    {
    case dtblcoltype_t::String:
    case dtblcoltype_t::Asset:
    case dtblcoltype_t::AssetNoPrecache:
        return true;
    default:
        return false;
    }
}

void Assets::AddDataTableAsset_v0(CPakFile* pak, std::vector<PakAsset_t>* assetEntries, const char* assetPath, rapidjson::Value& mapEntry)
{
    Debug("Adding dtbl asset '%s'\n", assetPath);
    Warning("!!!!! dtbl v0 is not implemented !!!!!\n");
}

// VERSION 8
void Assets::AddDataTableAsset_v1(CPakFile* pak, std::vector<PakAsset_t>* assetEntries, const char* assetPath, rapidjson::Value& mapEntry)
{
    Debug("Adding dtbl asset '%s'\n", assetPath);

    rapidcsv::Document doc(pak->GetAssetPath() + assetPath + ".csv");

    std::string sAssetName = assetPath;

    if (doc.GetColumnCount() < 0)
    {
        Warning("Attempted to add dtbl asset '%s' with no columns. Skipping asset...\n", assetPath);
        return;
    }

    if (doc.GetRowCount() < 2)
    {
        Warning("Attempted to add dtbl asset '%s' with invalid row count. Skipping asset...\nDTBL    - CSV must have a row of column types at the end of the table\n", assetPath);
        return;
    }

    CPakDataChunk hdrChunk = pak->CreateDataChunk(sizeof(datatable_t), SF_HEAD, 16);
    datatable_t* pHdr = reinterpret_cast<datatable_t*>(hdrChunk.Data());

    pHdr->numColumns = doc.GetColumnCount();
    pHdr->numRows = doc.GetRowCount()-1; // -1 because last row isnt added (used for type info)

    size_t colNameBufSize = 0;
    // get required size to store all of the column names in a single buffer
    for (auto& it : doc.GetColumnNames())
    {
        colNameBufSize += it.length() + 1;
    }

    CPakDataChunk colChunk = pak->CreateDataChunk(sizeof(datacolumn_t) * pHdr->numColumns, SF_CPU, 8);

    // column names
    CPakDataChunk colNameChunk = pak->CreateDataChunk(colNameBufSize, SF_CPU, 8);

    pHdr->pColumns = colChunk.GetPointer(); // { colhdrinfo.index, 0 };

    pak->AddPointer(hdrChunk.GetPointer(offsetof(datatable_t, pColumns)));

    // get a copy of the pointer because then we can shift it for each name
    char* colNameBuf = colNameChunk.Data();

    datacolumn_t* columns = reinterpret_cast<datacolumn_t*>(colChunk.Data());

    // vectors
    std::vector<std::string> typeRow = doc.GetRow<std::string>(pHdr->numRows);

    uint32_t colIdx = 0;
    // temp var used for storing the row offset for the next column in the loop below
    uint32_t tempColumnRowOffset = 0;
    uint32_t stringEntriesSize = 0;
    size_t rowDataPageSize = 0;

    for (auto& it : doc.GetColumnNames())
    {
        // copy the column name into the namebuf
        snprintf(colNameBuf, it.length() + 1, "%s", it.c_str());

        dtblcoltype_t type = GetDataTableTypeFromString(typeRow[colIdx]);

        datacolumn_t& col = columns[colIdx];

        // get number of bytes that we've added in the name buf so far
        col.pName = colNameChunk.GetPointer(colNameBuf - colNameChunk.Data());
        col.rowOffset = tempColumnRowOffset;
        col.type = type;

        // register name pointer
        pak->AddPointer(colChunk.GetPointer((sizeof(datacolumn_t) * colIdx) + offsetof(datacolumn_t, pName)));

        if (DataTable_IsStringType(type))
        {
            for (size_t i = 0; i < pHdr->numRows; ++i)
            {
                // this can be std::string since we only really need to deal with the string types here
                std::vector<std::string> row = doc.GetRow<std::string>(i);

                stringEntriesSize += row[colIdx].length() + 1;
            }
        }

        tempColumnRowOffset += DataTable_GetEntrySize(type);
        rowDataPageSize += DataTable_GetEntrySize(type) * pHdr->numRows; // size of type * row count (excluding the type row)
        
        colNameBuf += it.length() + 1;
        colIdx++;

        // if this is the final column, tempColumnRowOffset will now be equal to
        // the full size of the row data
        if (colIdx == pHdr->numColumns)
            pHdr->rowStride = tempColumnRowOffset;
    }

    // page for Row Data
    CPakDataChunk rowDataChunk = pak->CreateDataChunk(rowDataPageSize, SF_CPU, 8);

    // page for string entries
    CPakDataChunk stringChunk = pak->CreateDataChunk(stringEntriesSize, SF_CPU, 8);

    char* pStringBuf = stringChunk.Data();

    for (size_t rowIdx = 0; rowIdx < pHdr->numRows; ++rowIdx)
    {
        for (size_t colIdx = 0; colIdx < pHdr->numColumns; ++colIdx)
        {
            datacolumn_t& col = columns[colIdx];

            // get rmem instance for this cell's value buffer
            rmem valbuf(rowDataChunk.Data() + (pHdr->rowStride * rowIdx) + col.rowOffset);

            switch (col.type)
            {
            case dtblcoltype_t::Bool:
            {
                std::string val = doc.GetCell<std::string>(colIdx, rowIdx);

                std::transform(val.begin(), val.end(), val.begin(), ::tolower);

                if (val == "true")
                    valbuf.write<uint32_t>(true);
                else
                    valbuf.write<uint32_t>(false);
                break;
            }
            case dtblcoltype_t::Int:
            {
                uint32_t val = doc.GetCell<uint32_t>(colIdx, rowIdx);
                valbuf.write(val);
                break;
            }
            case dtblcoltype_t::Float:
            {
                float val = doc.GetCell<float>(colIdx, rowIdx);
                valbuf.write(val);
                break;
            }
            case dtblcoltype_t::Vector:
            {
                std::string val = doc.GetCell<std::string>(colIdx, rowIdx);

                std::smatch sm;

                // get values from format "<x,y,z>"
                std::regex_search(val, sm, s_VectorStringRegex);

                // 0 - all
                // 1 - x
                // 2 - y
                // 3 - z
                if (sm.size() == 4)
                {
                    float x = atof(sm[1].str().c_str());
                    float y = atof(sm[2].str().c_str());
                    float z = atof(sm[3].str().c_str());
                    Vector3 vec(x, y, z);

                    valbuf.write(vec);
                }
                break;
            }
            case dtblcoltype_t::String:
            case dtblcoltype_t::Asset:
            case dtblcoltype_t::AssetNoPrecache:
            {
                static uint32_t nextStringEntryOffset = 0;

                std::string val = doc.GetCell<std::string>(colIdx, rowIdx);
                snprintf(pStringBuf, val.length() + 1, "%s", val.c_str());

                valbuf.write(stringChunk.GetPointer(pStringBuf - stringChunk.Data()));
                pak->AddPointer(rowDataChunk.GetPointer((pHdr->rowStride * rowIdx) + col.rowOffset));

                pStringBuf += val.length() + 1;
                break;
            }
            }
        }
    }

    pHdr->pRows = rowDataChunk.GetPointer();

    pak->AddPointer(hdrChunk.GetPointer(offsetof(datatable_t, pRows)));

    PakAsset_t asset;

    asset.InitAsset(RTech::StringToGuid((sAssetName + ".rpak").c_str()), 
        hdrChunk.GetPointer(), hdrChunk.GetSize(),
        rowDataChunk.GetPointer(),
        -1, -1, (std::uint32_t)AssetType::DTBL);

    asset.version = DTBL_VERSION;

    asset.pageEnd = pak->GetNumPages();
    asset.remainingDependencyCount = 1; // asset only depends on itself

    assetEntries->push_back(asset);
}