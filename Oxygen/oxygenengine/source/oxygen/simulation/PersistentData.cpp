/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/simulation/PersistentData.h"
#include "oxygen/application/Configuration.h"


namespace
{
	const char* FORMAT_IDENTIFIER = "OXY.PDATA";
	const uint16 FORMAT_VERSION = 0x0100;		// First version
}


void PersistentData::clear()
{
	mFiles.clear();
}

void PersistentData::loadFromBasePath(const std::wstring& basePath)
{
	clear();

	mBasePath = basePath;
	FTX::FileSystem->normalizePath(mBasePath, true);

	if (!FTX::FileSystem->exists(mBasePath))
	{
		// First time setup: Create the directory and migrate previous "persistentdata.bin"
		initialSetup();
		return;
	}

	std::vector<rmx::FileIO::FileEntry> fileEntries;
	FTX::FileSystem->listFilesByMask(mBasePath + L"*.bin", true, fileEntries);

	std::vector<uint8> content;
	for (const rmx::FileIO::FileEntry& entry : fileEntries)
	{
		content.clear();
		if (!FTX::FileSystem->readFile(entry.mPath + entry.mFilename, content))
			continue;

		// Remove base path and the file extension
		std::wstring path = entry.mPath + entry.mFilename;
		RMX_ASSERT(rmx::startsWith(path, mBasePath), "Unexpected start of path");
		RMX_ASSERT(rmx::endsWith(path, L".bin"), "Unexpected ending of path");
		path.erase(0, mBasePath.length());
		path.erase(path.length() - 4, 4);

		const std::string filePath = WString(path).toStdString();
		const uint64 hash = rmx::getMurmur2_64(filePath);

		File& file = mFiles[hash];
		file.mFilePath = filePath;

		VectorBinarySerializer serializer(true, content);
		if (!serializeFile(file, serializer))
		{
			mFiles.erase(hash);
			continue;
		}
	}
}

const std::vector<uint8>& PersistentData::getData(uint64 filePathHash, uint64 keyHash) const
{
	static const std::vector<uint8> EMPTY;

	const File* file = mapFind(mFiles, filePathHash);
	if (nullptr == file)
		return EMPTY;

	const Entry* entry = findEntry(*file, keyHash);
	if (nullptr == entry)
		return EMPTY;

	return entry->mData;
}

void PersistentData::setData(std::string_view filePath, std::string_view key, const std::vector<uint8>& data)
{
	const uint64 filePathHash = rmx::getMurmur2_64(filePath);
	File* file = mapFind(mFiles, filePathHash);
	if (nullptr == file)
	{
		file = &mFiles[filePathHash];
		file->mFilePath = filePath;
	}

	const uint64 keyHash = rmx::getMurmur2_64(key);
	Entry* entry = findEntry(*file, keyHash);
	if (nullptr == entry)
	{
		// Create new entry
		entry = &vectorAdd(file->mEntries);
		entry->mKey = key;
		entry->mKeyHash = keyHash;
		entry->mData = data;
		saveFile(*file);
	}
	else
	{
		// Check for changes
		const bool anyChange = (entry->mData.size() != data.size()) || (memcmp(&entry->mData[0], &data[0], data.size()) != 0);
		if (anyChange)
		{
			entry->mData = data;
			saveFile(*file);
		}
	}
}

void PersistentData::removeKey(uint64 filePathHash, uint64 keyHash)
{
	File* file = mapFind(mFiles, filePathHash);
	if (nullptr == file)
		return;

	if (removeEntry(*file, keyHash))
	{
		if (file->mEntries.empty())
		{
			removeFile(*file);
		}
		else
		{
			saveFile(*file);
		}
	}
}

void PersistentData::initialSetup()
{
	FTX::FileSystem->createDirectory(mBasePath);

	std::vector<uint8> content;
	if (FTX::FileSystem->readFile(mBasePath + L"../persistentdata.bin", content))
	{
		const std::string filePath = "persistentdata";
		const uint64 hash = rmx::getMurmur2_64(filePath);

		File& file = mFiles[hash];
		file.mFilePath = filePath;

		VectorBinarySerializer serializer(true, content);
		serializeFile(file, serializer);

		// Save new file in its new location
		saveFile(file);

		// Rename the old file
		FTX::FileSystem->renameFile(mBasePath + L"../persistentdata.bin", mBasePath + L"../persistentdata.bin.backup");
	}
}

PersistentData::Entry* PersistentData::findEntry(File& file, uint64 keyHash)
{
	const auto it = std::find_if(file.mEntries.begin(), file.mEntries.end(), [&](const Entry& entry) { return entry.mKeyHash == keyHash; } );
	return (it != file.mEntries.end()) ? &*it : nullptr;
}

const PersistentData::Entry* PersistentData::findEntry(const File& file, uint64 keyHash) const
{
	const auto it = std::find_if(file.mEntries.begin(), file.mEntries.end(), [&](const Entry& entry) { return entry.mKeyHash == keyHash; } );
	return (it != file.mEntries.end()) ? &*it : nullptr;
}

bool PersistentData::removeEntry(File& file, uint64 keyHash)
{
	const auto it = std::find_if(file.mEntries.begin(), file.mEntries.end(), [&](const Entry& entry) { return entry.mKeyHash == keyHash; } );
	if (it == file.mEntries.end())
		return false;

	file.mEntries.erase(it);
	return true;
}

std::wstring PersistentData::getFullFilePath(const File& file) const
{
	return mBasePath + String(file.mFilePath).toStdWString() + L".bin";
}

bool PersistentData::removeFile(File& file)
{
	return FTX::FileSystem->removeFile(getFullFilePath(file));
}

bool PersistentData::saveFile(File& file)
{
	std::vector<uint8> content;
	VectorBinarySerializer serializer(false, content);
	if (!serializeFile(file, serializer))
		return false;

	return FTX::FileSystem->saveFile(getFullFilePath(file), content);
}

bool PersistentData::serializeFile(File& file, VectorBinarySerializer& serializer)
{
	// Identifier
	if (serializer.isReading())
	{
		char identifier[10];
		serializer.read(identifier, 9);
		if (memcmp(identifier, FORMAT_IDENTIFIER, 9) != 0)
			return false;
	}
	else
	{
		serializer.write(FORMAT_IDENTIFIER, 9);
	}

	// Format version
	uint16 formatVersion = FORMAT_VERSION;
	serializer& formatVersion;
	if (serializer.isReading())
	{
		RMX_CHECK(formatVersion >= 0x0100, "Invalid persistent data file format version", return false);
		RMX_CHECK(formatVersion <= FORMAT_VERSION, "Can't read persistent data file, as it's using a newer format version", return false);
	}

	// Data entries
	serializer.serializeArraySize(file.mEntries, 0xffffffff);
	if (serializer.isReading())
	{
		for (Entry& entry : file.mEntries)
		{
			serializer.serialize(entry.mKey);
			entry.mKeyHash = rmx::getMurmur2_64(entry.mKey);
			serializer.readData(entry.mData);
		}
	}
	else
	{
		for (const Entry& entry : file.mEntries)
		{
			serializer.write(entry.mKey);
			serializer.writeData(entry.mData);
		}
	}

	return true;
}
