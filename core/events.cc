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

#include <utils/utils.h>
#include <core/log.h>
#include <core/events.h>

namespace events {

Manager::Manager():
    m_running(true)
{
    pthread_cond_init(&m_queueCond, NULL);
}

void Manager::main_loop()
{
    do {
        m_queueMutex.lock();
        /* wait until there's events to process */
        pthread_cond_wait(&m_queueCond, m_queueMutex.get_mutex());
        m_queueMutex.unlock();
        while(!m_queue.empty()) {
            emit_event();
        }
    } while(m_running);
}

void Manager::emit_event()
{
    m_args = m_queue[0].second;

    /* stopping the current event works
     * by throwing StopEvent exception */
    try {
        (*m_queue[0].first)();
    } catch(StopEvent &e) {
        /* do nothing.. */
    }
    /* thrown if event handler calls arg()
     * and the conversion fails */
    catch(const boost::bad_any_cast &e) {
        core::Log::get()->log(e.what());
    }

    m_queueMutex.lock();
    m_queue.erase(m_queue.begin());
    m_queueMutex.unlock();
}

void Manager::create_event(const std::string &event)
    throw(std::logic_error)
{
    if(m_events.find(event) != m_events.end())
        throw std::logic_error("Event already exists");

    m_events[event] = new EventSig();
}

boost::signals::connection
Manager::add_listener(const std::string &event, EventFunc &func, Priority priority)
    throw(std::logic_error)
{
    if(m_events.find(event) == m_events.end()) {
        //throw std::logic_error("Event doesn't exist: " + event);
        m_events[event] = new EventSig();
    }

    return m_events[event]->connect(priority, func);
}


void Manager::emit(const std::string &event, boost::any a1,
        boost::any a2, boost::any a3,
        boost::any a4, boost::any a5)
{
    if(m_events.find(event) == m_events.end())
        return;

    AnyList args;
    if(!a1.empty())
        args.push_back(a1);
    if(!a2.empty())
        args.push_back(a2);
    if(!a3.empty())
        args.push_back(a3);
    if(!a4.empty())
        args.push_back(a4);
    if(!a5.empty())
        args.push_back(a5);

    m_queueMutex.lock();
    m_queue.push_back(std::make_pair(m_events[event], args));

    system(std::string("echo " + event + " >>emit").c_str());
    m_queueMutex.unlock();

    /* signal other thread there's data available */
    pthread_cond_signal(&m_queueCond);
}

Manager::~Manager()
{
    pthread_cond_destroy(&m_queueCond);
}

} // namespace events

