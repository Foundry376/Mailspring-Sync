/*
	vcard_grammar.cpp
	Copyright (C) 2017  Belledonne Communications SARL

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

#include "belcard/vcard_grammar.hpp"
#include "bctoolbox/logging.h"

std::shared_ptr<belr::Grammar> loadVcardGrammar() {
	std::shared_ptr<belr::Grammar> grammar = belr::GrammarLoader::get().load(VCARD_GRAMMAR);
	if (!grammar)
		bctbx_fatal("Unable to load VCARD grammar.");
	return grammar;
}
