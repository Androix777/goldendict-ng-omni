"""Run against the local server with the fixtures from browser_viewer.py enabled."""

import base64
import json
import re
import time
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import urlopen

from browser_viewer import URL, WORD


def get(endpoint, expected_status=200, **params):
    url = URL + "/api/v1/" + endpoint + "?" + urlencode(params)
    try:
        response = urlopen(url, timeout=35)
    except HTTPError as error:
        response = error
    with response:
        assert response.status == expected_status, (url, response.status)
        assert response.headers.get_content_type() == "application/json"
        return json.load(response)


def test_article_text():
    layout, = get("lookup", word="qt-layout", format="text")["articles"]
    text = layout["text"]
    for fragment in ("### Entry", "1. 前事柄（ことがら）後", "2. second", "7. seven", "3. three",
                     "2. two", "■ square", "朝食を食べる\neat breakfast", "語\t訳\n犬\tdog",
                     "left\tright", "前[glyph]後", "前後", "前字後", "expanded example"):
        assert fragment in text, (fragment, text)
    assert "invisible" not in text and "primarily-visual" not in layout["warnings"]
    scan, = get("lookup", word="qt-scan", format="text")["articles"]
    assert "[image]" in scan["text"] and "primarily-visual" in scan["warnings"], scan


def test_headwords():
    catalog = get("dictionaries")["dictionaries"]
    summary = next(d for d in catalog if d["name"] == "Article text fixture")
    assert summary["articleCount"] == summary["wordCount"] == 4 and summary["fileSizeBytes"] > 0
    dictionary = summary["id"]
    # A fresh profile builds its normal headword cache in the background.
    deadline = time.monotonic() + 30
    while True:
        info, = get("dictionaries", dictionary=dictionary)["dictionaries"]
        if info["headwordStatus"] == "ready":
            break
        assert info["headwordStatus"] == "unavailable" and time.monotonic() < deadline, info
        time.sleep(0.1)
    assert info["headwordCount"] == 3 and "Fixture for readable articles." in info["description"]
    def browse(**params):
        return get("headwords", dictionary=dictionary, **params)

    whole = browse()
    rows = [{"index": i, "word": word} for i, word in enumerate(("qt-layout", "qt-pair", "qt-scan"))]
    assert whole == {"dictionaryId": dictionary, "total": 3, "headwords": rows, "nextOffset": None}
    first = browse(limit=1)
    assert first["headwords"] == rows[:1] and first["nextOffset"] == 1
    assert browse(offset=first["nextOffset"])["headwords"] == rows[1:]
    assert browse(offset=3)["headwords"] == []
    assert browse(indices="1,0,1")["headwords"] == [rows[1], rows[0], rows[1]]
    assert browse(mode="sample", limit=1)["headwords"] == [rows[1]]
    assert browse(mode="sample", limit=5)["headwords"] == rows
    assert get("lookup", dictionary=dictionary, word=rows[1]["word"])["articles"]
    for params in ({"indices": "3"}, {"indices": "0,"}, {"indices": "-1"}, {"indices": "0", "offset": 0},
                   {"limit": 101}, {"limit": 0}, {"offset": -1}, {"mode": "sample", "offset": 1}):
        assert get("headwords", 400, dictionary=dictionary, **params)["error"]
    assert get("headwords", 400)["error"] and get("headwords", 404, dictionary="unknown")["error"]


def test_split_entries():
    html = get("lookup", word="qt-pair")["articles"]
    dictionary = html[0]["dictionaryId"]
    text = get("lookup", word="qt-pair", dictionary=dictionary, format="text")["articles"]
    assert len(html) == len(text) == 2
    assert sum("First homonym" in raw["html"] for raw in html) == 1
    for raw, readable in zip(html, text):
        assert raw["dictionaryId"] == readable["dictionaryId"] == dictionary
        assert raw["dictionaryName"] == readable["dictionaryName"] == "Article text fixture"
        assert "html" not in readable
        first = "First homonym" in raw["html"]
        assert ("First homonym" in readable["text"]) == first
        assert ("Second sense of first homonym" in readable["text"]) == first
        assert ("二" in readable["text"]) != first
        assert ("primarily-visual" in readable["warnings"]) != first


def test_api():
    groups = get("groups")["groups"]
    catalog = get("dictionaries", group=groups[0]["id"])["dictionaries"]
    assert catalog == get("dictionaries")["dictionaries"]
    fixtures = [d for d in catalog if d["name"] in ("Browser fixture A", "Browser fixture B")]
    assert len(fixtures) == 2 and all(d["enabled"] for d in fixtures)

    result = get("lookup", word=WORD, group=groups[0]["id"])
    assert result["word"] == WORD
    assert [a["dictionaryId"] for a in result["articles"]] == [d["id"] for d in fixtures]
    assert get("lookup", word=WORD, format="html") == result
    rendered = get("lookup", word=WORD, format="text", group=groups[0]["id"])
    assert rendered["word"] == WORD
    assert [a["dictionaryId"] for a in rendered["articles"]] == [d["id"] for d in fixtures]
    for item, dictionary in zip(rendered["articles"], fixtures):
        assert item["dictionaryName"] == dictionary["name"] and "html" not in item
        assert "子猫" in item["text"] and isinstance(item["warnings"], list)
        assert get("lookup", word=WORD, dictionary=dictionary["id"], format="text")["articles"] == [item]
    with urlopen(URL + "/lookup?" + urlencode({"word": WORD})) as response:
        fragments = dict(re.findall(r"id='gd-([^']+)' data-fragment='([^']*)'", response.read().decode()))
    for dictionary, article in zip(fixtures, result["articles"]):
        assert article["dictionaryName"] == dictionary["name"]
        assert article["html"] == base64.b64decode(fragments[dictionary["id"]]).decode()
        assert "子猫" in article["html"] and "<" in article["html"]
        assert get("lookup", word=WORD, dictionary=dictionary["id"])["articles"] == [article]

    assert get("lookup", word="missing_fixture_headword")["articles"] == []
    assert get("lookup", word="missing_fixture_headword", format="text")["articles"] == []
    assert get("lookup", 400, word=WORD, format="unknown")["error"]
    assert get("lookup", 400, word=WORD, format="")["error"]
    assert get("lookup", 400, word=" ")["error"]
    assert get("lookup", 400, word=WORD, group=1, dictionary="x")["error"]
    assert get("dictionaries", 400, group="bad")["error"]
    assert get("dictionaries", 404, group=0)["error"]
    assert get("lookup", 404, word=WORD, dictionary="unknown")["error"]


if __name__ == "__main__":
    test_api()
    test_article_text()
    test_split_entries()
    test_headwords()
    print("Dictionary API smoke test passed.")
