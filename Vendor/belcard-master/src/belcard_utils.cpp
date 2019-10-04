/*
	belcard_utils.cpp
	Copyright (C) 2015  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belcard/belcard_utils.hpp"
#include "bctoolbox/logging.h"

#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace::std;

string belcard_fold(const string &input) {
	string output = input;
	size_t crlf = 0;
	size_t next_crlf = 0;
	const char *endline = "\r\n";
	
	while (next_crlf != string::npos) {
		next_crlf = output.find(endline, crlf);
		if (next_crlf != string::npos) {
			if (next_crlf - crlf > 75) {
				output.insert(crlf + 74, "\r\n ");
				crlf += 76;
			} else {
				crlf = next_crlf + 2;
			}
		}
	}
	
	return output;
}

string belcard_unfold(const string &input) {
	string output = input;
	const char *endline = "\r\n";
	size_t crlf = output.find(endline);
	
	if (crlf == string::npos) {
		endline = "\n";
		crlf = output.find(endline);
	}
	
	while (crlf != string::npos) {
		if (isspace(output[crlf + strlen(endline)])) {
			output.erase(crlf, strlen(endline) + 1);
		} else {
			crlf += strlen(endline);
		}
		
		crlf = output.find(endline, crlf);
	}
	
	return output;
}

string belcard_read_file(const string &filename) {
	const char *fName = filename.c_str();
	ifstream istr(fName, ifstream::in | ifstream::binary);
	
	if (!istr || !istr.is_open() || istr.fail()) {
		bctbx_error("[belcard] Couldn't open file %s", fName);
		return string();
	}
	
	string vcard;
	istr.seekg(0, ios::end);
	vcard.resize((size_t)istr.tellg());
	istr.seekg(0, ios::beg);
	istr.read(&vcard[0], vcard.size());
	istr.close();
	return vcard;
}
