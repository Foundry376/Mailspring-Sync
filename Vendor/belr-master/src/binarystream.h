/*
 * Copyright (c) 2016-2019 Belledonne Communications SARL.
 *
 * This file is part of belr - a language recognition library for ABNF-defined grammars.
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef binarystream_h
#define binarystream_h

#include <fstream>
#include <memory>

namespace belr{
	
class Grammar;

/**
 * The BinaryInputStream is used internally to read grammars from a binary file.
**/
class BinaryInputStream : public std::ifstream{
public:
	unsigned char readUChar();
	int readInt();
	std::string readString();
};


inline BinaryInputStream &operator>>(BinaryInputStream &istr, unsigned char &val){
	val = istr.readUChar();
	return istr;
}

inline BinaryInputStream &operator>>(BinaryInputStream &istr, int &val){
	val = istr.readInt();
	return istr;
}

inline BinaryInputStream &operator>>(BinaryInputStream &istr, unsigned int &val){
	val = (unsigned int)istr.readInt();
	return istr;
}

inline BinaryInputStream &operator>>(BinaryInputStream &istr, std::string &val){
	val = istr.readString();
	return istr;
}

/**
 * The BinaryOutputStream is used internally to serialize grammars into a binary file.
**/
class BinaryOutputStream : public std::ofstream{
public:
	void writeUChar(unsigned char val);
	void writeInt(int val);
	void writeString(const std::string &val);
};


inline BinaryOutputStream &operator<<(BinaryOutputStream &ostr, unsigned char val){
	ostr.writeUChar(val);
	return ostr;
}

inline BinaryOutputStream &operator<<(BinaryOutputStream &ostr, int val){
	ostr.writeInt(val);
	return ostr;
}

inline BinaryOutputStream &operator<<(BinaryOutputStream &ostr, unsigned int val){
	ostr.writeInt((int)val);
	return ostr;
}

inline BinaryOutputStream &operator<<(BinaryOutputStream &ostr, const std::string& val){
	ostr.writeString(val);
	return ostr;
}

inline BinaryOutputStream &operator<<(BinaryOutputStream &ostr, const char * val){
	ostr.writeString(val);
	return ostr;
}

class Recognizer;

class BinaryGrammarBuilder : public BinaryInputStream{
public:
	BinaryGrammarBuilder(Grammar &grammar);
	std::shared_ptr<Recognizer> getRule(const std::string &name);
private:
	Grammar &mGrammar;
};


}//end of namespace
#endif
