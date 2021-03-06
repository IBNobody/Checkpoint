/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include "title.hpp"

static std::unordered_map<u128, std::vector<Title>> titles;
static std::unordered_map<u64, std::pair<u8*, u8*>> icons;

void freeIcons(void)
{
    for (auto& i : icons)
    {
        free(i.second.first);
        free(i.second.second);
    }
}

static void loadIcon(u64 id, NsApplicationControlData* nsacd, size_t iconsize)
{
    auto it = icons.find(id);
    if (it == icons.end())
    {
        uint8_t* imageptr = NULL;
        size_t imagesize = 256*256*3;
        
        njInit();
        if (njDecode(nsacd->icon, iconsize) != NJ_OK)
        {
            njDone();
            return;
        }

        if (njGetWidth() != 256 || njGetHeight() != 256 || (size_t)njGetImageSize() != imagesize || njIsColor() != 1)
        {
            njDone();
            return;
        }

        imageptr = njGetImage();
        if (imageptr == NULL)
        {
            njDone();
            return;   
        }

        u8* mIcon = (u8*)malloc(imagesize);
        std::copy(imageptr, imageptr + imagesize, mIcon);

        u8* mSmallIcon = (u8*)malloc(128*128*3);
        downscaleRGBImg(mIcon, mSmallIcon, 256, 256, 128, 128);

        icons.insert({id, std::make_pair(mIcon, mSmallIcon)});

        imageptr = NULL;
        njDone();
    }
}

void Title::init(u8 saveDataType, u64 id, u128 userID, const std::string& name, const std::string& author)
{
    mId = id;
    mUserId = userID;
    mSaveDataType = saveDataType;
    mUserName = Account::username(userID);
    mAuthor = author;
    mDisplayName = name;
    mSafeName = StringUtils::containsInvalidChar(name) ? StringUtils::format("0x%016llX", mId) : StringUtils::removeForbiddenCharacters(name);
    mPath = "sdmc:/switch/Checkpoint/saves/" + StringUtils::format("0x%016llX", mId) + " " + mSafeName;

    if (!io::directoryExists(mPath))
    {
        io::createDirectory(mPath);
    }

    refreshDirectories();
}

bool Title::systemSave(void)
{
    return mSaveDataType != FsSaveDataType_SaveData;
}

u8 Title::saveDataType(void)
{
    return mSaveDataType;
}

u64 Title::id(void)
{
    return mId;
}

u128 Title::userId(void)
{
    return mUserId;
}

std::string Title::userName(void)
{
    return mUserName;
}

std::string Title::author(void)
{
    return mAuthor;
}

std::string Title::name(void)
{
    return mDisplayName;
}

std::string Title::path(void)
{
    return mPath;
}

std::string Title::fullPath(size_t index)
{
    return mFullSavePaths.at(index);
}

std::vector<std::string> Title::saves()
{
    return mSaves;
}

u8* Title::icon(void)
{
    auto it = icons.find(mId);
    return it != icons.end() ? it->second.first : NULL;
}

u8* Title::smallIcon(void)
{
    auto it = icons.find(mId);
    return it != icons.end() ? it->second.second : NULL;
}

void Title::refreshDirectories(void)
{
    mSaves.clear();
    mFullSavePaths.clear();

    Directory savelist(mPath);
    if (savelist.good())
    {
        for (size_t i = 0, sz = savelist.size(); i < sz; i++)
        {
            if (savelist.folder(i))
            {
                mSaves.push_back(savelist.entry(i));
                mFullSavePaths.push_back(mPath + "/" + savelist.entry(i));
            }
        }
        
        std::sort(mSaves.rbegin(), mSaves.rend());
        std::sort(mFullSavePaths.rbegin(), mFullSavePaths.rend());
        mSaves.insert(mSaves.begin(), "New...");
        mFullSavePaths.insert(mFullSavePaths.begin(), "New...");
    }
    else
    {
        Gui::createError(savelist.error(), "Couldn't retrieve the directory list for the title " + name() + ".");
    }

    // save backups from configuration
    std::vector<std::string> additionalFolders = Configuration::getInstance().additionalSaveFolders(mId);
    for (std::vector<std::string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); it++)
    {
        // we have other folders to parse
        Directory list(*it);
        if (list.good())
        {
            for (size_t i = 0, sz = list.size(); i < sz; i++)
            {
                if (list.folder(i))
                {
                    mSaves.push_back(list.entry(i));
                    mFullSavePaths.push_back(*it + "/" + list.entry(i));
                }
            }
        }
    }
}

void loadTitles(void)
{
    titles.clear();

    FsSaveDataIterator iterator;
    FsSaveDataInfo info;
    size_t total_entries = 0;
    size_t outsize = 0;

    NacpLanguageEntry* nle = NULL;
    NsApplicationControlData* nsacd = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (nsacd == NULL)
    {
        return;
    }
    memset(nsacd, 0, sizeof(NsApplicationControlData));
    
    Result res = fsOpenSaveDataIterator(&iterator, FsSaveDataSpaceId_NandUser);
    if (R_FAILED(res))
    {
        return;
    }

    while(1)
    {
        res = fsSaveDataIteratorRead(&iterator, &info, 1, &total_entries);
        if (R_FAILED(res) || total_entries == 0)
        {
            break;
        }

        if (info.SaveDataType == FsSaveDataType_SaveData)
        {
            u64 tid = info.titleID;
            u128 uid = info.userID;
            if (!Configuration::getInstance().filter(tid))
            {
                res = nsGetApplicationControlData(1, tid, nsacd, sizeof(NsApplicationControlData), &outsize);
                if (R_SUCCEEDED(res) && !(outsize < sizeof(nsacd->nacp)))
                {
                    res = nacpGetLanguageEntry(&nsacd->nacp, &nle);
                    if (R_SUCCEEDED(res) && nle != NULL)
                    {
                        Title title;
                        title.init(info.SaveDataType, tid, uid, std::string(nle->name), std::string(nle->author));
                        loadIcon(tid, nsacd, outsize - sizeof(nsacd->nacp));

                        // check if the vector is already created
                        std::unordered_map<u128, std::vector<Title>>::iterator it = titles.find(uid);
                        if (it != titles.end())
                        {
                            // found
                            it->second.push_back(title);
                        }
                        else
                        {
                            // not found, insert into map
                            std::vector<Title> v;
                            v.push_back(title);
                            titles.emplace(uid, v);
                        }
                    }
                }
                nle = NULL;
            }

        }
    }

    free(nsacd);
    fsSaveDataIteratorClose(&iterator);

    for (auto& vect : titles)
    {
        std::sort(vect.second.begin(), vect.second.end(), [](Title& l, Title& r) {
            return l.name() < r.name();
        });
    }
}

void getTitle(Title &dst, u128 uid, size_t i)
{
    std::unordered_map<u128, std::vector<Title>>::iterator it = titles.find(uid);
    if (it != titles.end() && i < getTitleCount(uid))
    {
        dst = it->second.at(i);
    }
}

size_t getTitleCount(u128 uid)
{
    std::unordered_map<u128, std::vector<Title>>::iterator it = titles.find(uid);
    return it != titles.end() ? it->second.size() : 0;
}

void refreshDirectories(u64 id)
{
    for (auto& pair : titles)
    {
        for (size_t i = 0; i < pair.second.size(); i++)
        {
            if (pair.second.at(i).id() == id)
            {
                pair.second.at(i).refreshDirectories();
            }
        }
    }
}

u8* smallIcon(u128 uid, size_t i)
{
    std::unordered_map<u128, std::vector<Title>>::iterator it = titles.find(uid);
    return it != titles.end() ? it->second.at(i).smallIcon() : NULL; 
}