/*
	bctoolbox
	Copyright (C) 2016  Belledonne Communications SARL.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef exception_h
#define exception_h

#include <exception>
#include <sstream>

#include "bctoolbox/port.h"

#ifdef _WIN32
#pragma warning (push)
#pragma warning (disable : 4275 4251)
#endif

/**
* @brief General pupose exception saving backtrace.
*
* sample of use:
* try {
*		throw BCTBX_EXCEPTION << "Hello, this is my exception";
* } catch (BctbxException e&) {
*    BCTOOLBOX_SLOGD("mylogdomain") << "Exception cauth"<< e;
* }
*
*
*/
class BCTBX_PUBLIC BctbxException : public std::exception {
public:
	BctbxException(const std::string &message = "");
	BctbxException(const BctbxException &other);
	virtual ~BctbxException() = default;

	/**
	 * print stack strace to stderr
	 * */
	void printStackTrace() const;

	void printStackTrace(std::ostream &os) const;

	const char *what() const noexcept override;

	const std::string& str() const;

	/* same as osstringstream, but as osstream does not have cp contructor, BctbxException can't inherit from
	 * osstream*/
	template <typename T2> BctbxException &operator<<(const T2 &val) {
		mOs << val;
		return *this;
	}

private:
	void *mArray[20];
	size_t mSize;
	std::ostringstream mOs;
	mutable std::string mMessage;
};

BCTBX_PUBLIC std::ostream &operator<<(std::ostream &__os, const BctbxException &e);

#define BCTBX_EXCEPTION BctbxException() << " " << __FILE__ << ":" << __LINE__ << " "

#ifdef _WIN32
#pragma warning (pop)
#endif

#endif /* exception_h */
