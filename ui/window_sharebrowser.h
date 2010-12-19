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


#ifndef _WINDOWSHAREBROWSER_H_
#define _WINDOWSHAREBROWSER_H_

#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/DirectoryListing.h>
#include <display/directory_window.h>

namespace ui {

typedef DirectoryListing::Directory Dir;
typedef DirectoryListing::File File;

class WindowShareBrowser:
    public display::DirectoryWindow
{
public:
    WindowShareBrowser(User::Ptr user, const std::string &path);

    //virtual void create_list(DirectoryListing::Directory::List dirs, display::Directory *parent);
    virtual void create_list();
    void create_tree(Dir *parent, display::Directory *parent2);

    ~WindowShareBrowser() { }
private:
    User::Ptr m_user;
    DirectoryListing m_listing;
    std::string m_path;
    class ShareItem:
        public display::Item
    {
        public:
            ShareItem(DirectoryListing::File *file):m_file(file){ }
            std::string get_value() { return m_file->getName(); }
            std::string get_line(unsigned int line) { return m_file->getTTH().toBase32(); }
            ~ShareItem() { }
        private:
            DirectoryListing::File *m_file;

    };
};

} // namespace ui

#endif // _WINDOWSHAREBROWSER_H_
