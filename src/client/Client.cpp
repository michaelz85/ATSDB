/*
 * This file is part of ATSDB.
 *
 * ATSDB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ATSDB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with ATSDB.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Client.cpp
 *
 *  Created on: Jul 30, 2009
 *      Author: sk
 */
#include <QApplication>
#include "Logger.h"
#include "Client.h"

#include <QMessageBox>

using namespace std;

//namespace ATSDB
//{

Client::Client(int& argc, char ** argv)
    : QApplication(argc, argv)
{
}

bool Client::notify(QObject * receiver, QEvent * event)
{
    try
    {
        return QApplication::notify(receiver, event);
    }
    catch(std::exception& e)
    {
        logerr  << "Client: Exception thrown: " << e.what();
        QMessageBox::critical( NULL, "Client::notify(): Exception", QString( e.what() ) );
    }
    catch(...)
    {
        QMessageBox::critical( NULL, "Client::notify(): Exception", "Unknown exception" );
    }
    return false;
}
//}
