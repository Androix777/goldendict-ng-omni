"""End-to-end smoke test for a running local viewer, using an isolated profile.

python tests/browser_viewer.py --fixtures PATH creates small DSL and GLS fixtures in a
new directory. Add that directory to GoldenDict, then File > Open Local Browser
Viewer. With Python Playwright and Chromium installed, run this script again.
"""

import argparse
import base64
import wave
from pathlib import Path

URL = "http://127.0.0.1:18742"
WORD = "猫 + cat"


def write_fixtures(directory):
    directory.mkdir(parents=True, exist_ok=False)
    glossary = Path(__file__).with_name("fixtures") / "article_text.gls"
    (directory / glossary.name).write_bytes(glossary.read_bytes())
    for label in ("A", "B"):
        content = f'#NAME "Browser fixture {label}"\n#INDEX_LANGUAGE "Japanese"\n#CONTENTS_LANGUAGE "English"\n'
        content += f"{WORD}\nKitten alias\n [b]Definition {label}[/b] 子猫 [ref]Second fixture word[/ref] [s]pixel.png[/s] [s]tone.wav[/s]\n"
        content += "Second fixture word\n Another definition.\n"
        (directory / f"fixture-{label}.dsl").write_text(content, encoding="utf-16")
    (directory / "pixel.png").write_bytes(base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="
    ))
    with wave.open(str(directory / "tone.wav"), "wb") as audio:
        audio.setparams((1, 1, 8000, 0, "NONE", "not compressed"))
        audio.writeframes(bytes([128]) * 800)


def test_viewer(browser):
    page = browser.new_page()
    from playwright.sync_api import expect

    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(URL)
    navigation = page.get_by_role("navigation", name="Dictionaries", include_hidden=True)
    expect(navigation).to_be_hidden()
    inversion = page.get_by_role("checkbox", name="Invert colors")
    expect(inversion).not_to_be_checked()
    inversion.check()
    expect(page.locator("#gd-viewer")).to_have_css("filter", "invert(1)")
    expect(page.locator("html")).to_have_css("filter", "none")
    expect(page.locator("body")).to_have_css("filter", "none")
    page.reload()
    expect(inversion).to_be_checked()
    page.get_by_role("searchbox").fill(WORD)
    page.get_by_role("button", name="Look up").click()
    for label in ("A", "B"):
        section = page.locator("article").filter(has=page.get_by_role("heading", name=f"Browser fixture {label}"))
        expect(section.get_by_text(f"Definition {label}")).to_be_visible()
        assert section.locator('img[alt="pixel.png"]').evaluate("async img => { await img.decode(); return img.naturalWidth === 1; }")
    assert page.locator("main").evaluate("el => el.innerText.includes('子猫')")
    links = navigation.get_by_role("link")
    names = page.locator("main > article > h2").all_text_contents()
    expect(links).to_have_text(names)
    for i, heading in enumerate(page.locator("main > article > h2").all()):
        icon = heading.locator(".viewer-dict-icon")
        assert icon.evaluate("async img => { await img.decode(); return img.naturalWidth > 0; }")
        expect(links.nth(i).locator("img")).to_have_attribute("src", icon.get_attribute("src"))
    page.set_viewport_size({"width": 1200, "height": 260})
    page.evaluate("window.scrollTo(0, document.documentElement.scrollHeight)")
    expect(links.last).to_have_attribute("aria-current", "location")
    links.first.click()
    expect(links.first).to_have_attribute("aria-current", "location")
    links.last.click()
    expect(links.last).to_have_attribute("aria-current", "location")
    page.set_viewport_size({"width": 360, "height": 540})
    picker = navigation.get_by_role("combobox")
    picker.select_option(label=names[0])
    expect(page.locator("article:target > h2")).to_have_text(names[0])
    expect(navigation.locator("label img")).to_be_visible()
    selected_icon = page.locator("article:target > h2 img")
    expect(navigation.locator("label img")).to_have_attribute("src", selected_icon.get_attribute("src"))
    page.set_viewport_size({"width": 1200, "height": 720})
    first = page.locator("article").filter(has_text="Browser fixture A")
    first.locator(".dsl_s_wav a").click()
    audio = page.locator("audio#pronunciation")
    assert audio.is_visible()
    assert page.request.get(audio.get_attribute("src")).body().startswith(b"RIFF")
    first.get_by_text("Second fixture word", exact=True).click()
    expect(page.get_by_text("Another definition.", exact=True).first).to_be_visible()
    expect(inversion).to_be_checked()
    inversion.uncheck()
    page.goto(URL + "/lookup?word=Kitten+alias")
    expect(inversion).not_to_be_checked()
    expect(page.get_by_text("Definition A", exact=True)).to_be_visible()
    page.goto(URL + "/lookup?word=missing_fixture_headword")
    expect(page.get_by_text("No entries found.")).to_be_visible()
    expect(navigation).to_be_hidden()
    assert page.request.get(URL + "/lookup?group=invalid").status == 404
    assert not errors, errors
    page.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path)
    args = parser.parse_args()
    if args.fixtures:
        write_fixtures(args.fixtures)
    else:
        from playwright.sync_api import sync_playwright

        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            test_viewer(browser)
            browser.close()
        print("Browser viewer smoke test passed.")
