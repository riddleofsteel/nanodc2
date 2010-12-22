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

#include <functional>
#include <core/log.h>
#include <utils/utils.h>
#include <ui/window_sharebrowser.h>

namespace ui {

WindowShareBrowser::WindowShareBrowser(User::Ptr user, const std::string &path):
    m_listing(user),
    m_path(path)
{
    m_insertMode = false;
    set_title("Browsing user " + user->getFirstNick());
    set_name("List:" + user->getFirstNick());
    try {
        m_listing.loadFile(path);
        m_root = new display::Directory(0);
        m_current = m_root;

        create_tree(m_listing.getRoot(), m_root);
        create_list();
    } catch(Exception &e) {
        core::Log::get()->log("Cannot open list '" + path + "'" +
                                  " for user " + user->getFirstNick());
    }
}

void WindowShareBrowser::create_tree(Dir *parent, display::Directory *parent2)
{
    display::Directory *current = new display::Directory(parent2);
    parent2->get_children().push_back(current);
    std::for_each(parent->directories.begin(), parent->directories.end(),
            std::bind(&WindowShareBrowser::create_tree, this,
                std::placeholders::_1, current));

    for(File::List::iterator i = parent->files.begin(); i != parent->files.end(); ++i) {
        current->append_child(new ShareItem(*i));
        core::Log::get()->log((*i)->getName());
    }

}

void WindowShareBrowser::create_list(/*display::Directory *parent*/)
{
    /*set_title(m_current->getName());

    DirectoryListing::Directory dirs = m_current->directories;
    for(Dir::Iter it = dirs.begin(); it != dirs.end(); it++) {
        m_dirView->set_text(0, m_dirView->insert_row(), (*it)->getName());
    }

    DirectoryListing::File files = m_current->files;
    for(File::List::iterator i = files.begin(); i != files.end(); ++i) {
        m_fileView->set_text(0, m_fileView->insert_row(), (*i)->getName());
    }
    m_windowupdated(this);*/
}

#if 0
void WindowShareBrowser::create_list()
{
/*    typedef DirectoryListing::Directory Dir;
    typedef DirectoryListing::File File;
    set_title(m_current->getName());
    display::Directory *currentDir = new display::Directory(m_current);

    DirectoryListing::Directory dirs = m_current->directories;
    for(Dir::Iter it = dirs.begin(); it != dirs.end(); it++) {
        m_dirView->set_text(0, m_dirView->insert_row(), (*it)->getName());
    }

    DirectoryListing::File files = m_current->files;
    for(File::List::iterator i = files.begin(); i != files.end(); ++i) {
        m_fileView->set_text(0, m_fileView->insert_row(), (*i)->getName());
    }
    m_windowupdated(this);*/
}


void WindowShareBrowser::create_list(DirectoryListing::Directory::List dirs, display::Directory *parent)
{
    typedef DirectoryListing::Directory Dir;
    typedef DirectoryListing::File File;
    display::Directory *child = new display::Directory(parent);

    for(Dir::Iter it = dirs.begin(); it != dirs.end(); it++) {
        for(File::List::iterator i=(*it)->files.begin(); i != (*it)->files.end(); ++i) {
            int row = m_fileView->insert_row();
            m_fileView->set_text(0, row, (*i)->getName());
            if(parent->get_parent() == 0) {
                set_current(child);
            }
            child->append_child(new ShareItem(*i));
        }
        int row = m_dirView->insert_row();
        m_dirView->set_text(0, row, (*it)->getName());
        create_list((*it)->directories, child);
    }
    m_windowupdated(this);
}

#endif

} // namespace ui
