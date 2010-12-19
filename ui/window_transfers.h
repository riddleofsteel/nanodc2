/* vim:set ts=4 sw=4 sts=4 et cindent: */
/*
 * nanodc - The ncurses DC++ client
 * Copyright © 2005-2006 Markus Lindqvist <nanodc.developer@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Contributor(s):
 *  
 */

#ifndef _WINDOWTRANSFERS_H_
#define _WINDOWTRANSFERS_H_

#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/DownloadManager.h>
#include <client/UploadManager.h>
#include <client/ConnectionManager.h>
#include <client/ClientManager.h>
#include <client/ConnectionManagerListener.h>
#include <client/TimerManager.h>
#include <client/User.h>
#include <core/log.h>
#include <display/listview.h>
#include <utils/mutex.h>
#include <ext/hash_map>
#include <vector>
#include <string>
#include <map>

namespace ui {

class TransferItem
{
public:
    friend class WindowTransfers;
    TransferItem(User::Ptr user, bool isDownload):
        m_user(user), m_isDownload(isDownload),
        m_left(-1), m_size(-1), m_bytes(-1)
    { }

    bool is_download() const { return m_isDownload; }
private:
    User::Ptr m_user;
    bool m_isDownload;

    std::string m_file;
    std::string m_path;
    std::string m_target;
    int m_progress;
    char m_status;
    int64_t m_left;
    int64_t m_size;
    int64_t m_speed;
    int64_t m_started;
    int64_t m_bytes;
};

class WindowTransfers:
    public display::ListView,
    public DownloadManagerListener,
    public ConnectionManagerListener,
    public UploadManagerListener,
    public TimerManagerListener
{
public:
    WindowTransfers();
    ~WindowTransfers();

    typedef pair<User::Ptr, bool> UserID;


    /** Called when a transfer is completed. */
    void transfer_completed(Transfer *transfer);

    std::string get_infobox_line(unsigned int n);

    void force();
    void msg();
    void add_favorite();
    void remove_source();
    void disconnect();
    /** Disconnects the selected download and removes it from the queue. */
    void remove_download();

    virtual void on(TimerManagerListener::Second, u_int32_t) throw();

    virtual void on(ConnectionManagerListener::Added, ConnectionQueueItem *item) throw();
    virtual void on(ConnectionManagerListener::Removed, ConnectionQueueItem *item) throw();
    virtual void on(ConnectionManagerListener::Failed, ConnectionQueueItem *item, const string &reason) throw();
    virtual void on(ConnectionManagerListener::StatusChanged, ConnectionQueueItem *item) throw();

    virtual void on(DownloadManagerListener::Starting, Download *dl) throw();
    virtual void on(DownloadManagerListener::Tick, const Download::List &list) throw();
    virtual void on(DownloadManagerListener::Complete, Download *dl) throw() { transfer_completed(dl); }
    virtual void on(DownloadManagerListener::Failed, Download *dl, const string &reason) throw();

    virtual void on(UploadManagerListener::Starting, Upload *ul) throw();
    virtual void on(UploadManagerListener::Tick, const Upload::List &list) throw();
    virtual void on(UploadManagerListener::Complete, Upload *ul) throw() { transfer_completed(ul); }
private:
    int get_row(const std::string &cid, bool download);
    TransferItem *get_transfer(User::Ptr user, bool download);

    User::Ptr get_user();


    std::map<std::string, TransferItem*> m_transfers;
    utils::Mutex m_mutex;
};
} // namespace ui

#endif // _WINDOWTRANSFERS_H_
