/*
 *
 * Copyright (C) 2013  Maxim Noah Khailo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gui/contactlist.hpp"
#include "gui/util.hpp"
#include "util/dbc.hpp"

#include <QtWidgets>
#include <QGridLayout>
#include <QLabel>

#include <cstdlib>
#include <sstream>

namespace u = fire::util;
namespace n = fire::network;
namespace us = fire::user;
                
namespace fire
{
    namespace gui
    {
        namespace
        {
            const size_t TIMER_SLEEP = 200;//in milliseconds
        }

        std::string user_text(
                us::user_info_ptr c, 
                us::user_service_ptr s, 
                bool force_offline = false)
        {
            REQUIRE(c);
            REQUIRE(s);

            bool online = !force_offline && s->contact_available(c->id());

            std::stringstream ss;
            ss << "<font color='" << (online ? "green" : "red") << "'>" << c->name() << "</font>";
            return ss.str();
        }

        user_info::user_info(
                us::user_info_ptr p, 
                us::user_service_ptr s,
                bool compact,
                bool remove) :
            _contact{p},
            _service{s}
        {
            REQUIRE(p)
            REQUIRE(s)

            auto* layout = new QGridLayout{this};
            setLayout(layout);

            _user_text = new QLabel{user_text(p, _service).c_str()};
            _user_text->setToolTip(p->address().c_str());

            if(compact)
            {
                layout->addWidget(_user_text, 0,0);
                if(remove)
                {
                    _rm = new QPushButton("x");
                    _rm->setMaximumSize(20,20);
                    layout->addWidget(_rm, 0,1);
                    connect(_rm, SIGNAL(clicked()), this, SLOT(remove()));
                }
            }
            else
            {
                layout->addWidget( new QLabel{"Name:"}, 0,0);
                layout->addWidget( new QLabel{p->name().c_str()}, 0,1);
                layout->addWidget( new QLabel{"Address:"}, 1,0);
                layout->addWidget( new QLabel{p->address().c_str()}, 1,1);
            }

            layout->setContentsMargins(2,2,2,2);
            ENSURE(_contact);
            ENSURE(_user_text);
        }

        void user_info::update()
        {
            INVARIANT(_service);
            INVARIANT(_contact);
            INVARIANT(_user_text);

            _user_text->setText(user_text(_contact, _service).c_str());
        }

        void user_info::update(std::function<bool(us::user_info&)> f)
        {
            INVARIANT(_service);
            INVARIANT(_contact);
            INVARIANT(_user_text);

            _user_text->setText(user_text(_contact, _service, !f(*_contact)).c_str());
        }

        void user_info::remove()
        {
            INVARIANT(_service);
            INVARIANT(_contact);
            INVARIANT(_rm);
            INVARIANT(_user_text);

            std::stringstream msg;
            msg << "Are you sure you want to remove `" << _contact->name() << "'?";
            auto a = QMessageBox::warning(this, tr("Remove Contact?"), msg.str().c_str(), QMessageBox::Yes | QMessageBox::No);
            if(a != QMessageBox::Yes) return;

            _service->remove_contact(_contact->id());

            _rm->setEnabled(false);
            _user_text->setEnabled(false);
        }

        contact_list_dialog::contact_list_dialog(
                const std::string& title, 
                us::user_service_ptr service,
                bool add_on_start,
                QWidget* parent) :
            QDialog{parent},
            _service{service}
        {
            REQUIRE(service);

            //create tabs
            auto* layout = new QVBoxLayout{this};
            setLayout(layout);
            auto* tabs = new QTabWidget{this};
            layout->addWidget(tabs);
            
            auto* contacts_tab = new QWidget;
            auto* contacts_layout = new QGridLayout{contacts_tab};

            auto* greeters_tab = new QWidget;
            auto* greeters_layout = new QGridLayout{greeters_tab};
            tabs->addTab(contacts_tab, "contacts");
            tabs->addTab(greeters_tab, "greeters");

            init_contacts_tab(contacts_tab, contacts_layout, add_on_start);
            init_greeters_tab(greeters_tab, greeters_layout);

            setWindowTitle(tr(title.c_str()));

            INVARIANT(_list);
        }

        void contact_list_dialog::init_contacts_tab(QWidget* tab, QGridLayout* layout, bool add_on_start)
        {
            REQUIRE(tab);
            REQUIRE(layout);
            INVARIANT(_service);

            //create contact list
            _list = new list;
            layout->addWidget(_list, 0, 0, 2, 3);

            update_contacts();

            //create add button
            auto* add_new = new QPushButton("add");
            layout->addWidget(add_new, 2,0); 
            connect(add_new, SIGNAL(clicked()), this, SLOT(new_contact()));

            //create create invite button
            auto* invite = new QPushButton("create invite");
            layout->addWidget(invite, 2,1); 
            connect(invite, SIGNAL(clicked()), this, SLOT(create_contact_file()));

            //create id label
            std::string id = _service->user().info().id(); 
            auto* id_label = new QLabel("your id");
            auto* id_txt = new QLineEdit(id.c_str());
            id_txt->setMinimumWidth(id.size() * 8);
            id_txt->setReadOnly(true);
            id_txt->setFrame(false);
            layout->addWidget(id_label, 3,0); 
            layout->addWidget(id_txt, 3,1,1,2); 

            std::string addr = _service->in_host() + ":" + _service->in_port();
            auto* addr_label = new QLabel("your address");
            auto* addr_txt = new QLineEdit(addr.c_str());
            addr_txt->setMinimumWidth(addr.size() * 8);
            addr_txt->setReadOnly(true);
            addr_txt->setFrame(false);
            layout->addWidget(addr_label, 4,0); 
            layout->addWidget(addr_txt, 4,1,1,2); 

            //setup updated timer
            auto *t = new QTimer(this);
            connect(t, SIGNAL(timeout()), this, SLOT(update()));
            t->start(TIMER_SLEEP);

            if(add_on_start) new_contact();
        }

        void contact_list_dialog::init_greeters_tab(QWidget* tab, QGridLayout* layout)
        {
            REQUIRE(tab);
            REQUIRE(layout);
            INVARIANT(_service);
            auto gl = new greeter_list{_service};

            layout->addWidget(gl, 0,0);
            auto* add_new = new QPushButton("add");
            layout->addWidget(add_new, 1,0); 
            connect(add_new, SIGNAL(clicked()), gl, SLOT(add_greeter()));

        }

        void contact_list_dialog::update_contacts()
        {
            INVARIANT(_list);
            INVARIANT(_service);

            _list->clear();

            for(auto u : _service->user().contacts().list())
                _list->add(new user_info{u, _service, true, true});

            _prev_contacts = _service->user().contacts().size();
        }

        std::string greet_address(const us::greet_server& s)
        {
            return s.host() + ":" + s.port();
        }

        void contact_list_dialog::new_contact()
        {
            INVARIANT(_service);

            //get file name to load
            std::string home = std::getenv("HOME");
            auto file = QFileDialog::getOpenFileName(this,
                    tr("Open Invite File"), home.c_str(), tr("Invite File (*.finvite)"));

            if(file.isEmpty()) { return;}

            //load contact file
            us::contact_file cf;
            if(!us::load_contact_file(convert(file), cf)) { return; }

            //add greeter
            if(!cf.greeter.empty())
            {
                bool found = false;
                for(const auto& s : _service->user().greeters())
                {
                    auto a = greet_address(s);
                    if(a == cf.greeter) {found = true; break;}
                }
                if(found) cf.greeter = "";
            }

            //add contact
            _service->confirm_contact(cf);
        }

        void contact_list_dialog::update()
        {
            size_t contacts = _service->user().contacts().size();
            if(contacts == _prev_contacts) 
                return;

            update_contacts();
            _prev_contacts = contacts;
        }

        contact_list::contact_list(us::user_service_ptr service, const us::contact_list& contacts, bool remove) :
            _service{service},
            _remove{remove}
        {
            REQUIRE(service);
            for(auto u : contacts.list())
            {
                CHECK(u);
                add_contact(u);
            }

            INVARIANT(_service);
        }

        void contact_list::add_contact(us::user_info_ptr u)
        {
            REQUIRE(u);
            INVARIANT(_service);
            if(_contacts.by_id(u->id())) return;

            auto ui = new user_info{u, _service, true, _remove};
            add(ui);
            _contacts.add(u);
            _contact_widgets.push_back(ui);
        }

        void contact_list::update(const us::contact_list& contacts)
        {
            clear();
            _contact_widgets.clear();
            _contacts.clear();
            for(auto c : contacts.list())
            {
                CHECK(c);
                add_contact(c);
            }

            ENSURE_LESS_EQUAL(_contacts.size(), contacts.size());
        }

        void contact_list::update_status()
        {
            for(auto p : _contact_widgets)
            {
                CHECK(p);
                p->update();
            }
        }

        void contact_list::update_status(std::function<bool(us::user_info&)> f)
        {
            for(auto p : _contact_widgets)
            {
                CHECK(p);
                p->update(f);
            }
        }

        greeter_list::greeter_list(us::user_service_ptr service) :
            _service{service}

        {
            REQUIRE(service);
            INVARIANT(_service);
            for(const auto& s : _service->user().greeters())
                add_greeter(s);
        }
                
        void greeter_list::add_greeter(const us::greet_server& s)
        {
            add(new greeter_info{_service, s});
        }

        void greeter_list::add_greeter()
        {
            bool ok = false;
            bool error = false;
            std::string address = "localhost";

            do
            {
                try
                {
                    QString r = QInputDialog::getText(
                            0, 
                            (error ? "Error, try again" : "Add New Greeter"),
                            "Greeter Address",
                            QLineEdit::Normal, address.c_str(), &ok);

                    if(ok && !r.isEmpty()) address = convert(r);
                    else return;

                    error = false;
                }
                catch(...)
                {
                    error = true;
                }
            }
            while(error);

            //append port if not specified
            if(address.find(":") == std::string::npos) address.append(":7070");

            auto host_port = n::parse_host_port(address);

            _service->add_greeter(address);
            us::greet_server ts{host_port.first, host_port.second, ""};
            add_greeter(ts);
        }

        greeter_info::greeter_info(us::user_service_ptr service, const us::greet_server& s) :
            _server{s}, _service{service}
        {
            INVARIANT(_service);

            auto* layout = new QGridLayout{this};
            setLayout(layout);
            _address = _server.host() + ":" + _server.port();
            _label = new QLabel{_address.c_str()};
            layout->addWidget( _label, 0,0);
            _rm = new QPushButton("x");
            _rm->setMaximumSize(20,20);
            layout->addWidget(_rm, 0,1);
            connect(_rm, SIGNAL(clicked()), this, SLOT(remove()));

        }

        void greeter_info::remove()
        {
            INVARIANT(_service);

            std::stringstream msg;
            msg << "Are you sure you want to remove `" << _address << "'?";
            auto a = QMessageBox::warning(this, tr("Remove Greeter?"), msg.str().c_str(), QMessageBox::Yes | QMessageBox::No);
            if(a != QMessageBox::Yes) return;

            _service->remove_greeter(_address);
            _label->setEnabled(false);
            _rm->setEnabled(false);
        }

        void contact_list_dialog::create_contact_file()
        {
            //have user select contact file 
            std::string home = std::getenv("HOME");
            std::string default_file = home + "/" + _service->user().info().name() + ".finvite";
            auto file = QFileDialog::getSaveFileName(this, tr("Save File"),
                    default_file.c_str(),
                    tr("Invite File (*.finvite)"));

            if(file.isEmpty())
                return;

            //user chooses greeter
            auto total_greeters = _service->user().greeters().size();
            std::string greeter;
            if(total_greeters == 1) greeter = greet_address(_service->user().greeters().front());
            else if(total_greeters > 1)
            {
                QStringList gs;
                for(const auto& g : _service->user().greeters())
                    gs << greet_address(g).c_str();

                bool ok;
                auto g = QInputDialog::getItem(this, tr("Suggested Greeter"),
                        tr("Choose a greeter:"), gs, 0, false, &ok);
                if (ok && !g.isEmpty())
                    greeter = convert(g);
            }

            //save contact file
            us::contact_file cf{ _service->user().info(), greeter};
            us::save_contact_file(convert(file), cf);
        }
    }
}
