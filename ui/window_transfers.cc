
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include <sstream>
#include <iomanip>
#include <tr1/functional>
#include <ui/window_transfers.h>
#include <ui/window_privatemessage.h>
#include <ui/window_hub.h>
#include <display/manager.h>
#include <display/screen.h>
#include <client/Util.h>
#include <client/QueueManager.h>
#include <client/FavoriteManager.h>
#include <client/ConnectionManager.h>
#include <core/events.h>
#include <utils/utils.h>
#include <utils/lock.h>
#include <core/log.h>

namespace ui {


WindowTransfers::WindowTransfers()
{
    DownloadManager::getInstance()->addListener(this);
    ConnectionManager::getInstance()->addListener(this);
    UploadManager::getInstance()->addListener(this);
    TimerManager::getInstance()->addListener(this);
    set_title("Transfer window");
    set_name("transfers");

    insert_column(new display::Column("ID"));
    insert_column(new display::Column("Flags", 3, 6, 6));
    insert_column(new display::Column("Nick", 8, 10, 20));
    insert_column(new display::Column("Speed", 13, 13, 13));
    insert_column(new display::Column("%%", 6, 6, 6));
    insert_column(new display::Column("Filename/Status", 50, 100, 200));
    resize();

    m_bindings['m'] = std::tr1::bind(&WindowTransfers::msg, this);
    m_bindings['F'] = std::tr1::bind(&WindowTransfers::force, this);
    m_bindings['f'] = std::tr1::bind(&WindowTransfers::add_favorite, this);
    m_bindings['r'] = std::tr1::bind(&WindowTransfers::remove_source, this);
    m_bindings['c'] = std::tr1::bind(&WindowTransfers::disconnect, this);
    m_bindings['R'] = std::tr1::bind(&WindowTransfers::remove_download, this);

    // browse
    m_bindings['b'] = std::tr1::bind(&QueueManager::addList, QueueManager::getInstance(),
                        std::tr1::bind(&WindowTransfers::get_user, this), QueueItem::FLAG_CLIENT_VIEW);
    // match queue
    m_bindings['M'] = std::tr1::bind(&QueueManager::addList, QueueManager::getInstance(),
                        std::tr1::bind(&WindowTransfers::get_user, this), QueueItem::FLAG_MATCH_QUEUE);

    // grant a slot
    m_bindings['g'] = std::tr1::bind(&UploadManager::reserveSlot,
                        UploadManager::getInstance(),
                        std::tr1::bind(&WindowTransfers::get_user, this));
}

User::Ptr WindowTransfers::get_user()
{
    int row = get_selected_row();
    if(row == -1)
        return User::Ptr();

    std::string text = get_text(0, get_selected_row());
    return m_transfers[text]->m_user;
}

int WindowTransfers::get_row(const std::string &cid, bool download)
{
    std::string id = (download ? "d":"u") + cid;
    int row = find_row(0, id);
    if(row == -1) {
        row = insert_row();
        set_text(0, row, id);
    }
    return row;
}

TransferItem* WindowTransfers::get_transfer(User::Ptr user, bool download)
{
    std::string id = (download ? "d":"u") + user->getCID().toBase32();

    if(m_transfers.find(id) != m_transfers.end()) {
        return m_transfers[id];
    }
    TransferItem *item = new TransferItem(user, download);
    m_transfers[id] = item;
    return item;
}

void WindowTransfers::remove_download()
{
    utils::Lock l(m_mutex);

    int row = get_selected_row();
    if(row == -1)
        return;

    std::string id = get_text(0, row);
    /* downloads only */
    if(id[0] != 'd')
        return;

    TransferItem *item = get_transfer(get_user(), true);
    QueueManager::getInstance()->remove(item->m_target);
}

void WindowTransfers::add_favorite()
{
    utils::Lock l(m_mutex);

    int row = get_selected_row();
    if(row == -1)
        return;

    User::Ptr user = get_user();
    FavoriteManager::getInstance()->addFavoriteUser(user);
}

void WindowTransfers::remove_source()
{
    User::Ptr user = get_user();
    QueueManager::getInstance()->removeSource(user, QueueItem::Source::FLAG_REMOVED);
}

void WindowTransfers::msg()
{
    StringList hubs = ClientManager::getInstance()->getHubNames(get_user()->getCID());
/*
    display::Manager *dm = display::Manager::get();
    for(unsigned int i=0; i<hubs.size(); ++i) {
        for(unsigned int j=0; j<dm->size(); ++j) {
            if((*dm)[j]->get_name() == hubs[i]) {
                std::string my_nick = dynamic_cast<ui::WindowHub*>((*dm)[j])->get_nick();
                dm->push_back(new ui::WindowPrivateMessage(get_user(), my_nick));
                return;
            }
        }
    }
    core::Log::get()->log("User not in hub: " + get_user()->getFirstNick());
    */
}

void WindowTransfers::disconnect()
{
    utils::Lock l(m_mutex);

    int row = get_selected_row();
    if(row == -1)
        return;

    std::string id = get_text(0, row);
    ConnectionManager::getInstance()->disconnect(get_user(), (id[0] == 'd'));
}

void WindowTransfers::force()
{
    utils::Lock l(m_mutex);

    int row = get_selected_row();
    if(row == -1)
        return;

    ClientManager::getInstance()->connect(get_user());
    set_text(5, get_selected_row(), "Connecting (forced)");
}

void WindowTransfers::transfer_completed(Transfer *transfer)
{
    bool isDownload = transfer->getUserConnection().isSet(UserConnection::FLAG_DOWNLOAD);
    User::Ptr user = transfer->getUserConnection().getUser();

    int row = get_row(user->getCID().toBase32(), isDownload);
    set_text(4, row, "100");

    TransferItem *item = get_transfer(user, isDownload);
    item->m_left = -1;
}

void WindowTransfers::on(TimerManagerListener::Second, u_int32_t) throw() {
    events::emit("window updated", dynamic_cast<display::Window*>(this));
}

// ConnectionManager
void WindowTransfers::on(ConnectionManagerListener::Added, ConnectionQueueItem *cqi)
    throw()
{
    utils::Lock l(m_mutex);

    int row = get_row(cqi->getUser()->getCID().toBase32(), cqi->getDownload());

    set_text(2, row, cqi->getUser()->getFirstNick());
    set_text(4, row, "0");
    set_text(5, row, "Connecting...");

    TransferItem *item = get_transfer(cqi->getUser(), cqi->getDownload());
    item->m_started = GET_TICK();
}

void WindowTransfers::on(ConnectionManagerListener::StatusChanged, ConnectionQueueItem *cqi) throw()
{
    utils::Lock l(m_mutex);

    int row = get_row(cqi->getUser()->getCID().toBase32(), cqi->getDownload());

    if(cqi->getState() == ConnectionQueueItem::CONNECTING)
        set_text(5, row, "Connecting...");
    else
        set_text(5, row, "Waiting to retry");
}

void WindowTransfers::on(ConnectionManagerListener::Removed, ConnectionQueueItem *cqi) throw()
{
return;
    utils::Lock l(m_mutex);

    UserID id(cqi->getUser(), cqi->getDownload());
    delete_row(0, (id.second?"d":"u") + id.first->getCID().toBase32());
}

void WindowTransfers::on(ConnectionManagerListener::Failed, ConnectionQueueItem *cqi, const std::string &reason) throw()
{
    utils::Lock l(m_mutex);

    int row = get_row(cqi->getUser()->getCID().toBase32(), cqi->getDownload());
    set_text(3, row, "");
    set_text(5, row, reason);
}

// DownloadManager
void WindowTransfers::on(DownloadManagerListener::Starting, Download *dl) throw()
{
    utils::Lock l(m_mutex);

    int row = get_row(dl->getUserConnection().getUser()->getCID().toBase32(), true);
    std::string target = Text::acpToUtf8(dl->getTarget());

    if(dl->isSet(Download::FLAG_USER_LIST))
        set_text(5, row, "Starting: Filelist");
    else
        set_text(5, row, "Starting: " + Util::getFileName(target));

    TransferItem *item = get_transfer(dl->getUserConnection().getUser(), true);
    item->m_path = Util::getFilePath(target);
    item->m_size = dl->getSize();
    item->m_started = GET_TICK();
    item->m_target = target;
}

void WindowTransfers::on(DownloadManagerListener::Tick, const Download::List &list) throw()
{
    utils::Lock l(m_mutex);

    Download::List::const_iterator it;
    for(it = list.begin(); it != list.end(); it++) {
        Download *dl = *it;

        std::string flags = "D";
        if(dl->getUserConnection().isSecure())
            flags += "S";
        if(dl->isSet(Download::FLAG_ZDOWNLOAD))
            flags += "Z";
        if(dl->isSet(Download::FLAG_ROLLBACK))
            flags += "R";

        User::Ptr user = dl->getUserConnection().getUser();
        int row = get_row(user->getCID().toBase32(), true);

        set_text(1, row, flags);
        set_text(2, row, user->getFirstNick());
        set_text(3, row, Util::formatBytes(dl->getRunningAverage()) + "/s");
        set_text(4, row, utils::to_string(static_cast<int>((dl->getPos() * 100.0) / dl->getSize())));
        set_text(5, row, Util::getFileName(Text::acpToUtf8(dl->getTarget())));

        TransferItem *item = get_transfer(user, true);
        item->m_left = dl->getSecondsLeft();
        item->m_size = dl->getSize();
        item->m_bytes = dl->getPos();
        item->m_target = dl->getTarget();
    }
}

void WindowTransfers::on(DownloadManagerListener::Failed, Download *dl, const std::string &reason) throw()
{
    utils::Lock l(m_mutex);

    User::Ptr user = dl->getUserConnection().getUser();
    int row = get_row(user->getCID().toBase32(), true);
    set_text(5, row, reason);

    TransferItem *item = get_transfer(user, true);
    std::string target = Text::acpToUtf8(dl->getTarget());

    if (dl->isSet(Download::FLAG_USER_LIST))
        item->m_file = "Filelist";
    else
        item->m_file = Util::getFileName(target);

    item->m_path = Util::getFilePath(target);
}

// UploadManager
void WindowTransfers::on(UploadManagerListener::Starting, Upload *ul) throw()
{
    utils::Lock l(m_mutex);

    TransferItem *item = get_transfer(ul->getUserConnection().getUser(), false);
    std::string target = Text::acpToUtf8(ul->getSourceFile());

    if (ul->isSet(Download::FLAG_USER_LIST))
        item->m_file = "Filelist";
    else
        item->m_file = Util::getFileName(target);

    item->m_path = Util::getFilePath(target);
    item->m_size = ul->getSize();
    item->m_started = GET_TICK();
}

void WindowTransfers::on(UploadManagerListener::Tick, const Upload::List &list) throw()
{
    utils::Lock l(m_mutex);

    for(Upload::List::const_iterator it = list.begin(); it != list.end(); it++) {
        Upload *ul = *it;

        std::ostringstream stream;

        std::string flags = "U";
        if(ul->getUserConnection().isSecure())
            flags += "S";
        if(ul->isSet(Upload::FLAG_ZUPLOAD))
            flags += "Z";

        User::Ptr user = ul->getUserConnection().getUser();
        int row = get_row(user->getCID().toBase32(), false);
        set_text(1, row, flags);
        set_text(2, row, user->getFirstNick());
        set_text(3, row, Util::formatBytes(ul->getRunningAverage()) + "/s");
        set_text(4, row, utils::to_string(static_cast<int>((ul->getPos() * 100.0) / ul->getSize())));
        set_text(5, row, ul->getSourceFile());

        TransferItem *item = get_transfer(user, false);
        item->m_left = ul->getSecondsLeft();
        item->m_bytes = ul->getPos();
        item->m_size = ul->getSize();
    }
}

WindowTransfers::~WindowTransfers()
{
    DownloadManager::getInstance()->removeListener(this);
    ConnectionManager::getInstance()->removeListener(this);
    UploadManager::getInstance()->removeListener(this);
    TimerManager::getInstance()->removeListener(this);
}

std::string WindowTransfers::get_infobox_line(unsigned int n)
{
    return std::string(); // @todo ListView needs locking before this function can be run without crashes.

    std::string text = get_text(0, get_selected_row());
    TransferItem *item = get_transfer(get_user(), (text[0] == 'd'));

    std::ostringstream oss;
    switch(n) {
        case 1:
        {
            oss << "%21Elapsed:%21 " << Util::formatSeconds((GET_TICK()-item->m_started) / 1000);
            if(item->m_left != -1)
                oss << " %21Left:%21 " << Util::formatSeconds(item->m_left);

            if(item->m_bytes != -1 && item->m_size != -1)
                oss << " %21Transferred:%21 " << Util::formatBytes(item->m_bytes)
                    << "/" << Util::formatBytes(item->m_size);
            break;
        }
        case 2:
            oss << item->m_path << item->m_file;
            break;
        case 3:
        {
            StringList hubs = ClientManager::getInstance()->getHubNames(get_user()->getCID());
            oss << "%21Hubs:%21 " + (hubs.empty() ? std::string("(Offline)") : Util::toString(hubs));
            break;
        }
        case 4:
        {
            if(item->m_bytes == -1 || item->m_size == -1)
                break;

            double percent = static_cast<double>(item->m_bytes)/item->m_size;
            int width = display::Screen::get_width()-2;
            oss << "%21#%21" << std::string(static_cast<int>(width*percent), '#') + 
                std::string(static_cast<int>(width*(1-percent)), ' ') + "%21#%21";
        }
    }
    return oss.str();
}

}
