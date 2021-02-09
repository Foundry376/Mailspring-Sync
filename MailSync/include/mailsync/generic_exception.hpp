/** GenericException [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
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

#ifndef GenericException_hpp
#define GenericException_hpp

#include <stdio.h>
#include "nlohmann/json.hpp"
#include "StanfordCPPLib/stacktrace/call_stack.h"

using namespace nlohmann;
using namespace std;

class GenericException : public std::exception {
    vector<stacktrace::entry> _stackentries;

public:
    GenericException();
    void printStackTrace();

    virtual json toJSON();
};


#endif /* GenericException_hpp */
