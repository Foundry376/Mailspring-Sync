//
//  vcard_test.cpp
//  Unit tests for VCard property lookup and display-name derivation.
//
//  Build and run (Linux): cmake . && make vcard_test && ./vcard_test
//

#include "VCard.hpp"

#include <iostream>

static int failures = 0;

static void expectEqual(const string & actual, const string & expected, const string & label) {
    if (actual != expected) {
        cerr << "FAIL " << label << ": expected \"" << expected << "\", got \"" << actual << "\"\n";
        failures++;
    }
}

static string card(const string & body) {
    return "BEGIN:VCARD\r\nVERSION:3.0\r\n" + body + "END:VCARD\r\n";
}

int main() {
    {
        // Apple Contacts writes multi-value properties with a group prefix (RFC 6350 §3.3).
        VCard vcard(card(
            "FN:Lloyd\r\n"
            "item1.EMAIL;type=INTERNET;type=pref:Melloyd@aol.com\r\n"
            "item1.X-ABLabel:INTERNET\r\n"
            "ITEM2.TEL;type=CELL:+15555550100\r\n"));
        expectEqual(vcard.getEmails().front()->getValue(), "Melloyd@aol.com", "grouped EMAIL");
        expectEqual(to_string(vcard.getEmails().size()), "1", "grouped EMAIL count");
        expectEqual(vcard.displayName(), "Lloyd", "FN wins");

        // The group prefix survives serialization, and no placeholder lines are added.
        string out = vcard.serialize();
        expectEqual(to_string(out.find("ITEM1.EMAIL;type=INTERNET;type=pref:Melloyd@aol.com\r\n") != string::npos), "1", "grouped EMAIL serialized");
        expectEqual(to_string(out.find("ITEM2.TEL;type=CELL:+15555550100\r\n") != string::npos), "1", "grouped TEL serialized");
        expectEqual(to_string(out.find("\r\nEMAIL:") == string::npos), "1", "no placeholder EMAIL");
    }
    {
        VCard vcard(card("FN:Plain\r\nEMAIL:plain@example.com\r\nitem1.EMAIL:grouped@example.com\r\n"));
        expectEqual(vcard.getEmails().front()->getValue(), "plain@example.com", "first EMAIL in card order");
        expectEqual(to_string(vcard.getEmails().size()), "2", "plain and grouped EMAIL count");
    }
    {
        VCard vcard(card("item1.X-ADDRESSBOOKSERVER-KIND:group\r\n"));
        expectEqual(vcard.getExtendedProperties().front()->baseName(), "X-ADDRESSBOOKSERVER-KIND", "grouped X- property");
    }
    {
        // iOS 5: empty FN and an empty structured N.
        VCard vcard(card("N:;;;;\r\nFN:\r\nTEL:+15555550100\r\n"));
        expectEqual(vcard.getEmails().front()->getValue(), "", "no EMAIL");
        expectEqual(vcard.displayName(), "", "N:;;;; is not a name");
    }
    {
        VCard vcard(card("N:Ave;121;Roup;;\r\nFN:\r\nitem1.ADR:;;121 Roup Ave;;;;\r\n"));
        expectEqual(vcard.displayName(), "121 Roup Ave", "name from N components");
    }
    {
        VCard vcard(card("N:Smith;John;Quincy;Dr.;Jr.\r\n"));
        expectEqual(vcard.displayName(), "Dr. John Quincy Smith Jr.", "N with prefix and suffix");
    }
    {
        VCard vcard(card("N:O\\;Brien;Pat\\,Sam;;;\r\nFN:  \r\n"));
        expectEqual(vcard.displayName(), "Pat,Sam O;Brien", "escaped N components");
    }
    {
        VCard vcard(card("N:;;;;\r\nFN:\r\nORG:Acme\\, Inc.;Sales\r\n"));
        expectEqual(vcard.displayName(), "Acme, Inc.", "ORG fallback");
    }
    {
        VCard vcard(card("FN:Melloyd@aol com\r\n"));
        expectEqual(vcard.displayName(), "Melloyd@aol com", "FN taken as written");
    }

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "vcard_test: all passed\n";
    return 0;
}
