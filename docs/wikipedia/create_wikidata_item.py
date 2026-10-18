#!/usr/bin/env python3
"""Create the Wikidata item for orbisRPC, then print the new QID.

Needs ONE thing: a Wikidata bot password. Generate it at
  https://www.wikidata.org/wiki/Special:BotPasswords
(label it "orbisRPC-creator", grant the "Edit existing pages" + "Create,
edit, and move pages" rights). It prints a username like
"YourUser@orbisRPC-creator" and a random password.

Then run:
  WD_BOT_USER='YourUser@orbisRPC-creator' WD_BOT_PASS='the-random-password' \
      python3 docs/wikipedia/create_wikidata_item.py

Stdlib only. Idempotent: if an item labelled "orbisRPC" with the Discord
description already exists, it prints that QID and exits instead of duplicating.
"""
import json
import os
import sys
import urllib.parse
import urllib.request
import http.cookiejar

API = "https://www.wikidata.org/w/api.php"
UA = "orbisRPC-wikidata-creator/1.0 (https://github.com/SirHumza/orbisRPC)"

LABEL = "orbisRPC"
DESC = "Discord Rich Presence software for the PlayStation 4"
ALIASES = ["orbis rpc", "Orbis RPC", "orbisRPC PS4"]

REPO = "https://github.com/SirHumza/orbisRPC"
SITE = "https://sirhumza.github.io/orbisRPC/"


def item_claim(prop, qid):
    numeric = int(qid.lstrip("Q"))
    return {
        "mainsnak": {
            "snaktype": "value",
            "property": prop,
            "datavalue": {
                "value": {"entity-type": "item", "numeric-id": numeric, "id": qid},
                "type": "wikibase-entityid",
            },
        },
        "type": "statement",
        "rank": "normal",
    }


def url_claim(prop, url):
    return {
        "mainsnak": {
            "snaktype": "value",
            "property": prop,
            "datavalue": {"value": url, "type": "string"},
        },
        "type": "statement",
        "rank": "normal",
    }


def main():
    user = os.environ.get("WD_BOT_USER")
    password = os.environ.get("WD_BOT_PASS")
    if not user or not password:
        sys.exit(
            "Set WD_BOT_USER and WD_BOT_PASS (see docstring). Nothing sent."
        )

    jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    opener.addheaders = [("User-Agent", UA)]

    def call(params):
        params = dict(params)
        params["format"] = "json"
        data = urllib.parse.urlencode(params).encode()
        with opener.open(urllib.request.Request(API, data=data)) as resp:
            return json.load(resp)

    token = call({"action": "query", "meta": "tokens", "type": "login"})["query"][
        "tokens"
    ]["logintoken"]

    login = call(
        {
            "action": "login",
            "lgname": user,
            "lgpassword": password,
            "lgtoken": token,
        }
    )
    if login.get("login", {}).get("result") != "Success":
        sys.exit("Login failed: %s" % json.dumps(login.get("login", login)))

    csrf = call({"action": "query", "meta": "tokens", "type": "csrf"})["query"][
        "tokens"
    ]["csrftoken"]

    existing = call(
        {
            "action": "wbsearchentities",
            "search": LABEL,
            "language": "en",
            "type": "item",
            "limit": 5,
        }
    ).get("search", [])
    for hit in existing:
        if hit.get("label", "").lower() == "orbisrpc":
            print("Already exists: %s (%s)" % (hit["id"], hit.get("description", "")))
            print("https://www.wikidata.org/wiki/%s" % hit["id"])
            return

    entity = {
        "labels": {"en": {"language": "en", "value": LABEL}},
        "descriptions": {"en": {"language": "en", "value": DESC}},
        "aliases": {
            "en": [{"language": "en", "value": a} for a in ALIASES]
        },
        "claims": {
            "P31": [item_claim("P31", "Q506883")],
            "P400": [item_claim("P400", "Q5014725")],
            "P1324": [url_claim("P1324", REPO)],
            "P856": [url_claim("P856", SITE)],
            "P277": [item_claim("P277", "Q15777")],
        },
    }

    result = call(
        {
            "action": "wbeditentity",
            "new": "item",
            "token": csrf,
            "data": json.dumps(entity),
            "summary": "Create orbisRPC (PS4 Discord Rich Presence); maintainer disclosure on user page",
        }
    )

    if "error" in result:
        sys.exit("Create failed: %s" % json.dumps(result["error"]))

    qid = result["entity"]["id"]
    print("Created %s" % qid)
    print("https://www.wikidata.org/wiki/%s" % qid)
    print()
    print("Next: add %s to the sameAs array in site/index.html and"
          " site/about-orbisrpc.html, then commit + push." % qid)


if __name__ == "__main__":
    main()
