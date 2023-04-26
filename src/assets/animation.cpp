#include "pch.h"
#include "assets.h"
#include "public/studio.h"


void Assets::AddAnimSeqAsset_stub(CPakFile* pak, std::vector<PakAsset_t>* assetEntries, const char* assetPath, rapidjson::Value& mapEntry)
{
	Error("unsupported asset type 'aseq' for version 7\n");
}

void Assets::AddAnimSeqAsset_v7(CPakFile* pak, std::vector<PakAsset_t>* assetEntries, const char* assetPath, rapidjson::Value& mapEntry)
{
    Debug("Adding aseq asset '%s'\n", assetPath);

    AnimSequenceHeader* aseqHeader = new AnimSequenceHeader();

    std::string rseqFilePath = pak->GetAssetPath() + assetPath;

    // require rseq file to exist
    REQUIRE_FILE(rseqFilePath);

    int fileNameDataSize = strlen(assetPath) + 1;
    int rseqFileSize = (int)Utils::GetFileSize(rseqFilePath);

    uint32_t bufAlign = 4 - (fileNameDataSize + rseqFileSize) % 4;

    char* pDataBuf = new char[fileNameDataSize + rseqFileSize + bufAlign]{};

    // write the rseq file path into the data buffer
    snprintf(pDataBuf, fileNameDataSize, "%s", assetPath);

    // begin rseq input
    BinaryIO rseqInput;
    rseqInput.open(rseqFilePath, BinaryIOMode::Read);

    // go back to the beginning of the file to read all the data
    rseqInput.seek(0);

    // write the rseq data into the data buffer
    rseqInput.getReader()->read(pDataBuf + fileNameDataSize, rseqFileSize);
    rseqInput.close();

    mstudioseqdesc_t seqdesc = *reinterpret_cast<mstudioseqdesc_t*>(pDataBuf + fileNameDataSize);

    // Segments
    // asset header
    _vseginfo_t subhdrinfo = pak->CreateNewSegment(sizeof(AnimSequenceHeader), SF_HEAD, 16);

    // data segment
    _vseginfo_t dataseginfo = pak->CreateNewSegment(rseqFileSize + fileNameDataSize + bufAlign, SF_CPU, 64);

    aseqHeader->szname = { dataseginfo.index, 0 };

    aseqHeader->data = { dataseginfo.index, fileNameDataSize };

    pak->AddPointer(subhdrinfo.index, offsetof(AnimSequenceHeader, szname));
    pak->AddPointer(subhdrinfo.index, offsetof(AnimSequenceHeader, data));

    std::vector<PakGuidRefHdr_t> guids{};

    rmem dataBuf(pDataBuf);
    dataBuf.seek(fileNameDataSize + seqdesc.autolayerindex, rseekdir::beg);

    // register autolayer aseq guids
    for (int i = 0; i < seqdesc.numautolayers; ++i)
    {
        dataBuf.seek(fileNameDataSize + seqdesc.autolayerindex + (i * sizeof(mstudioautolayer_t)), rseekdir::beg);

        mstudioautolayer_t* autolayer = dataBuf.get<mstudioautolayer_t>();

        if (autolayer->guid != 0)
            pak->AddGuidDescriptor(&guids, dataseginfo.index, dataBuf.getPosition() + offsetof(mstudioautolayer_t, guid));

        PakAsset_t* asset = pak->GetAssetByGuid(autolayer->guid);

        if (asset)
            asset->AddRelation(assetEntries->size());
    }

    PakRawDataBlock_t shdb{ subhdrinfo.index, subhdrinfo.size, (uint8_t*)aseqHeader };
    pak->AddRawDataBlock(shdb);

    PakRawDataBlock_t rdb{ dataseginfo.index, dataseginfo.size, (uint8_t*)pDataBuf };
    pak->AddRawDataBlock(rdb);

    uint32_t lastPageIdx = dataseginfo.index;

    PakAsset_t asset;

    asset.InitAsset(RTech::StringToGuid(assetPath), subhdrinfo.index, 0, subhdrinfo.size, -1, 0, -1, -1, (std::uint32_t)AssetType::ASEQ);
    asset.version = 7;
    // i have literally no idea what these are
    asset.pageEnd = lastPageIdx + 1;
    asset.remainingDependencyCount = 2;

    asset.AddGuids(&guids);

    assetEntries->push_back(asset);
}