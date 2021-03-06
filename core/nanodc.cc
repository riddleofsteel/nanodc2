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

#include <iostream>
#include <fstream>
#include <memory>
#include <signal.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/stat.h>
#include <functional>
#include <utils/stacktrace.h>
#include <utils/utils.h>
#include <utils/signal_handler.h>
#include <core/nanodc.h>
#include <core/manager.h>

namespace core {

Nanodc::Nanodc(int argc, char **argv):
    m_argc(argc),
    m_argv(argv),
    m_crash(false)
{
    m_pidfile = utils::get_settingdir() + ".pid";
    add_signal_handlers();
    setlocale(LC_ALL, "");
}

int Nanodc::run()
{
    if(!check_pidfile())
        return 2;

    check_root() || raise(SIGKILL);

    try {
        core::Manager::create()->run();
    }
    catch(std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        core::Manager::destroy();
        erase(); refresh(); doupdate();
        endwin();
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }
    core::Manager::destroy();
    endwin();
    return 0;
}

void Nanodc::add_signal_handlers()
{
    std::function<void (uint32_t)> handler = std::bind(
            &Nanodc::handle_crash, this, std::placeholders::_1);

    utils::SignalHandler::add_handler(SIGINT, handler);
    utils::SignalHandler::add_handler(SIGILL, handler);
    utils::SignalHandler::add_handler(SIGFPE, handler);
    utils::SignalHandler::add_handler(SIGABRT, handler);
    utils::SignalHandler::add_handler(SIGSEGV, handler);
    utils::SignalHandler::add_handler(SIGTERM, handler);
}

bool Nanodc::check_root()
{
    if(getuid() == 0) {
        if(m_argc > 1 && std::strcmp(*(m_argv+1), "-root") == 0) {
            std::cout << "Do NOT run nanodc as root!!"
                "If you really know what you are doing use switch -im-an-idiot"
                << std::endl;
            return false;
        }
        if(m_argc > 1 && std::strcmp(*(m_argv+1), "-im-an-idiot") == 0)
            return true;
        std::cout << "Do not run nanodc as root! (Skip this check with -root)" << std::endl;
        return false;
    }
    return true;
}

bool Nanodc::check_pidfile()
{
    bool retval = true;
    struct stat buf;
    if (stat(m_pidfile.c_str(), &buf) == 0) {
        // skip pidfile check with -d
        if(m_argc < 2 || std::strcmp(*(m_argv+1), "-d") != 0)
        {
            std::cout << "Nanodc already running?\n"
                      << "Shut down nanodc or remove the pidfile: "
                      << m_pidfile << std::endl
                      << "(Skip this chech with -d)\n"
                      << "(E)xit or (R)emove the pidfile and start nanodc"
                      << std::endl;

            int ch = getchar();
            if(ch == 'R' || ch == 'r') {
                std::remove(m_pidfile.c_str());
            }
            else {
                retval = false;
            }
        }
    }

    // write the pidfile
    std::ofstream f;
    f.open(m_pidfile.c_str());
    f << getpid() << std::endl;
    f.close();
    return retval;
}

void Nanodc::handle_crash(int sig)
{
    if(m_crash)
        abort();

    m_crash = true;
    erase(); refresh(); doupdate();
    endwin();
    remove(m_pidfile.c_str());

    std::cerr << utils::SignalHandler::to_string(sig) << std::endl;
    std::cerr << "pid: " << getpid() <<
        ", tid: " << utils::gettid() << std::endl;

#if USE_STACKTRACE
    cow::StackTrace trace;
    trace.generate_frames();
    std::copy(trace.begin(), trace.end(),
        std::ostream_iterator<cow::StackFrame>(std::cerr, "\n"));
#else
    std::cerr << "Stacktrace is not enabled\n";
#endif

    exit(sig);
}

Nanodc::~Nanodc()
{
    remove(m_pidfile.c_str());
}

} // namespace core
