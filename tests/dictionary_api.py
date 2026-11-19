"""Run against the local server with the fixtures from browser_viewer.py enabled."""

import base64
import json
import re
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
    print("Dictionary API smoke test passed.")
