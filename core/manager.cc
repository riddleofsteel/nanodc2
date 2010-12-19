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

#include <boost/thread/thread.hpp>
// #include <tr1/functional>
#include <pthread.h>
#include <client/stdinc.h>
#include <client/DCPlusPlus.h>
#include <client/SettingsManager.h>
#include <client/TimerManager.h>
#include <client/LogManager.h>
#include <client/SearchManager.h>
#include <client/ConnectionManager.h>
#include <client/ClientManager.h>
#include <client/LogManager.h>
#include <client/HashManager.h>
#include <client/FavoriteManager.h>
#include <client/UploadManager.h>
#include <client/DownloadManager.h>
#include <client/ShareManager.h>
#include <client/ResourceManager.h>
#include <client/CryptoManager.h>
#include <client/QueueManager.h>
#include <client/FinishedManager.h>
#include <client/ADLSearch.h>

#include <ui/window_hub.h>
#include <input/manager.h>
#include <display/manager.h>
#include <core/manager.h>
#include <core/settings.h>
#include <utils/utils.h>
#include <ui/manager.h>
#include <core/events.h>
#include <core/log.h>

namespace core {

Manager::Manager()
{
}

void Manager::start_client()
{
    core::Log::get()->log("start_client: " + utils::to_string(utils::gettid()));
    Util::initialize();
    const char *charset;
    bool utf8 = g_get_charset(&charset);
    if(utf8) {
        if(!core::Settings::get()->exists("utf8_input"))
            core::Settings::get()->set("utf8_input", true);
        if(!core::Settings::get()->exists("nmdc_charset"))
            core::Settings::get()->set("nmdc_charset", "ISO-8859-15");
    }

    if(!core::Settings::get()->exists("command_char"))
        core::Settings::get()->set("command_char", "/");

    core::Log::get()->log("Starting the client...");
    ResourceManager::newInstance();
    SettingsManager::newInstance();

    LogManager::newInstance();
    HashManager::newInstance();
    CryptoManager::newInstance();
    SearchManager::newInstance();
    ClientManager::newInstance();
    ConnectionManager::newInstance();
    DownloadManager::newInstance();
    UploadManager::newInstance();
    ShareManager::newInstance();
    FavoriteManager::newInstance();
    QueueManager::newInstance();
    FinishedManager::newInstance();
    ADLSearchManager::newInstance();

    core::Log::get()->log("Loading: Settings");
    SettingsManager::getInstance()->load();

    core::Log::get()->log("Loading: Hash database");
    HashManager::getInstance()->startup();

    core::Log::get()->log("Loading: Shared files");
    ShareManager::getInstance()->setDirty();
    ShareManager::getInstance()->refresh(true);

    core::Log::get()->log("Loading: Download queue");
    QueueManager::getInstance()->loadQueue();

    core::Log::get()->log("Loading: Favorites");
    FavoriteManager::getInstance()->load();

    SearchManager::getInstance()->disconnect();
    ConnectionManager::getInstance()->disconnect();

    if (ClientManager::getInstance()->isActive()) {
        try {
            ConnectionManager::getInstance()->listen();
        } catch(const Exception &e) {
            core::Log::get()->log("StartSocket(tcp): " + e.getError() + " (other program blocking the port?)");
        }
        try {
            SearchManager::getInstance()->listen();
        } catch(const Exception &e) {
            core::Log::get()->log("StartSocket(udp): " + e.getError() + " (other program blocking the port?)");
        }
    }
    ClientManager::getInstance()->infoUpdated();

    events::emit("command motd", std::string());

    // set random default nick
    if(SETTING(NICK).empty()) {
        std::string nick = "nanodc" + utils::to_string(Util::rand(1000, 50000));
        SettingsManager::getInstance()->set(SettingsManager::NICK, nick);
        core::Log::get()->log("Note: Using nick " + nick);
    }

    if(SETTING(DESCRIPTION).empty()) {
        SettingsManager::getInstance()->set(SettingsManager::DESCRIPTION,
                "nanodc user (http//nanodc.sf.net)");
    }

    const char *http_proxy = getenv("http_proxy");
    if(http_proxy) {
        SettingsManager::getInstance()->set(SettingsManager::HTTP_PROXY, http_proxy);
        core::Log::get()->log("Note: Using HTTP proxy " + std::string(http_proxy));
    }

    if(SETTING(AUTO_REFRESH_TIME) == 60) {
        SettingsManager::getInstance()->set(SettingsManager::AUTO_REFRESH_TIME, 360);
        core::Log::get()->log("Note: Filelist refresh time set to 360");
    }

    if(!SETTING(LANGUAGE_FILE).empty()) {
        ResourceManager::getInstance()->loadLanguage(SETTING(LANGUAGE_FILE));
    }

    connect_hubs();

    events::emit("client created");
}

int Manager::run()
{
    std::string nanodcrc = utils::get_settingdir() + ".nanodcrc";
    core::Settings::create()->read(nanodcrc);

    core::Log::create();

    events::create("client created");

    ui::Manager::create()->init();

    /* /q is alias for /quit */
    events::add_listener("command q",
            std::tr1::bind(&events::emit, &std::string("command quit")));

    events::add_listener_last("command quit",
            std::tr1::bind(&events::Manager::quit, events::Manager::get()));

    events::add_listener("command quit",
            std::tr1::bind(&input::Manager::quit, input::Manager::get()));

    events::add_listener_first("command quit",
            std::tr1::bind(&core::Log::log, core::Log::get(),
                "Shutting down nanodc...", core::MT_MSG));

    TimerManager::newInstance();
    TimerManager::getInstance()->start();

    boost::thread input_thread(
                std::tr1::bind(&input::Manager::main_loop,
                    input::Manager::get()));

    boost::thread client_starter(
                std::tr1::bind(&Manager::start_client, this));

    core::Log::get()->log("Event loop: " + utils::to_string(utils::gettid()));
    events::Manager::get()->main_loop();

    input_thread.join();
    client_starter.join();
    return 0;
}

void Manager::connect_hubs()
{
    FavoriteHubEntry::List favhubs = FavoriteManager::getInstance()->getFavoriteHubs();
    FavoriteHubEntry::List::iterator it = favhubs.begin();
    for(;it != favhubs.end(); ++it) {
        if((*it)->getConnect()) {
            ui::WindowHub * hub = new ui::WindowHub((*it)->getServer());
            display::Manager::get()->push_back(hub);
            hub->connect((*it)->getServer(), "", "", "");
        }
    }
}

void Manager::shutdown()
{
    display::Manager::destroy();
    input::Manager::destroy();
    core::Log::destroy();
    core::Settings::destroy();

    ::shutdown();
}

Manager::~Manager()
{
    shutdown();
}

} // namespace core
