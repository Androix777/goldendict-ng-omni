// DOM preparation for the native off-screen reader. Each article gets a new document.
function restoreOrphanRows(html) {
  if (!/<tr[\s>]/i.test(html)) return html;
  const tags = /<!--[\s\S]*?(?:-->|$)|<(\/?)([a-z][\w:-]*)\b(?:[^'"<>]|"[^"]*"|'[^']*')*>/gi;
  const literal = new Set(["script", "style", "title", "textarea", "iframe", "xmp", "noembed", "noframes"]);
  let depth = 0, start = -1, match;
  const runs = [];
  while ((match = tags.exec(html))) {
    if (!match[2]) continue;
    const tag = match[2].toLowerCase(), closing = !!match[1];
    if (!closing && literal.has(tag)) {
      const end = new RegExp("</" + tag + "\\s*>", "ig");
      end.lastIndex = tags.lastIndex;
      tags.lastIndex = end.exec(html) ? end.lastIndex : html.length;
    } else if (tag === "table") {
      depth = Math.max(0, depth + (closing ? -1 : 1));
    } else if (tag === "tr" && !depth) {
      if (!closing) start = match.index;
      else if (start >= 0) {
        const previous = runs[runs.length - 1];
        if (previous && !html.slice(previous[1], start).trim()) previous[1] = tags.lastIndex;
        else runs.push([start, tags.lastIndex]);
        start = -1;
      }
    }
  }
  let tail = 0;
  const parts = [];
  for (const [begin, end] of runs) {
    parts.push(html.slice(tail, begin), "<table>", html.slice(begin, end), "</table>");
    tail = end;
  }
  return parts.join("") + html.slice(tail);
}

function generatedLabels(root, warnings) {
  const labels = {};
  let index = 0;
  // Computed CSS serializes literals as quoted strings, including CSS escapes.
  function unquote(s) {
    return s.slice(1, -1).replace(/\\([\da-f]{1,6})\s?|\\(.)/gi,
      (_, hex, ch) => hex ? String.fromCodePoint(Math.min(parseInt(hex, 16), 0x10ffff)) : ch);
  }
  function numeral(n, type) {
    if (!type || type === "decimal") return String(n);
    if (type === "decimal-leading-zero") return String(n).padStart(2, "0");
    if (/^(upper|lower)-(alpha|latin)$/.test(type) && n > 0) {
      let s = "";
      while (n > 0) { n--; s = String.fromCharCode(97 + n % 26) + s; n = Math.floor(n / 26); }
      return type.startsWith("upper") ? s.toUpperCase() : s;
    }
    if (/^(upper|lower)-roman$/.test(type) && n > 0 && n < 4000) {
      let s = "";
      for (const [value, digits] of [[1000,"M"],[900,"CM"],[500,"D"],[400,"CD"],[100,"C"],[90,"XC"],[50,"L"],[40,"XL"],[10,"X"],[9,"IX"],[5,"V"],[4,"IV"],[1,"I"]]) {
        while (n >= value) { s += digits; n -= value; }
      }
      return type.startsWith("upper") ? s : s.toLowerCase();
    }
    warnings.add("unsupported-css-counter");
    return String(n);
  }
  function content(style, el, counters) {
    const value = style.content;
    if (!value || value === "none" || value === "normal" || style.display === "none") return "";
    // A slash introduces alternative text for accessibility, outside the visual label.
    const tokens = /\s+|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|(?:url|counter|attr)\((?:[^)"']|"[^"]*"|'[^']*')*\)|\//gy;
    let result = "";
    while (tokens.lastIndex < value.length) {
      const match = tokens.exec(value);
      if (!match) { warnings.add("unsupported-css-content"); break; }
      const token = match[0];
      if (token === "/") break;
      if (/^\s/.test(token) || token.startsWith("url(")) continue;
      if (token.startsWith("attr(")) result += el.getAttribute(token.slice(5, -1)) || "";
      else if (token.startsWith("counter(")) {
        const [name, type] = token.slice(8, -1).split(",").map(s => s.trim());
        result += numeral(counters.get(name)?.value ?? 0, type);
      } else result += unquote(token);
    }
    return result;
  }
  function updateCounters(style, counters) {
    for (const [property, initial] of [["counterReset",0], ["counterSet",0], ["counterIncrement",1]]) {
      const rule = style[property];
      if (!rule || rule === "none") continue;
      for (const [,name,number] of rule.matchAll(/([^\s]+)(?:\s+(-?\d+))?/g)) {
        if (property === "counterReset" || !counters.has(name)) counters.set(name, {value: 0});
        const counter = counters.get(name), value = number === undefined ? initial : Number(number);
        counter.value = property === "counterIncrement" ? counter.value + value : value;
      }
    }
  }
  function visit(parent, inherited) {
    const scope = new Map(inherited);
    const reversed = parent.localName === "ol" && parent.reversed;
    let ordinal = parent.hasAttribute("start") && parent.localName === "ol" ? parent.start :
      reversed ? [...parent.children].filter(el => el.localName === "li").length : 1;
    for (const el of parent.children) {
      const id = index++;
      const style = getComputedStyle(el);
      if (style.display === "none") { index += el.querySelectorAll("*").length; continue; }
      updateCounters(style, scope);
      const listItem = style.display.includes("list-item");
      if (listItem && el.localName === "li" && el.hasAttribute("value")) ordinal = el.value;
      const childScope = new Map(scope);
      if (listItem) childScope.set("list-item", {value:ordinal});
      const before = getComputedStyle(el, "::before"), after = getComputedStyle(el, "::after");
      if (before.content !== "none" && before.content !== "normal") updateCounters(before, childScope);
      const parts = {before:content(before, el, childScope)};
      if (listItem) {
        const marker = getComputedStyle(el, "::marker");
        const type = style.listStyleType;
        const bullets = {disc:"•", circle:"◦", square:"■", disclosure_open:"▾", disclosure_closed:"▸"};
        if (marker.content !== "normal") parts.marker = content(marker, el, childScope);
        else if (/^["']/.test(type)) parts.marker = unquote(type);
        else if (type && type !== "none" && style.listStyleImage === "none") {
          parts.marker = bullets[type.replace("-", "_")] ?? (numeral(ordinal, type) + ".");
        }
        ordinal += reversed ? -1 : 1;
      }
      visit(el, childScope);
      if (after.content !== "none" && after.content !== "normal") updateCounters(after, childScope);
      parts.after = content(after, el, childScope);
      if (parts.before || parts.after || parts.marker) labels[id] = parts;
    }
  }
  visit(root, new Map());
  return labels;
}

function prepareTextArticle(id, html) {
  document.addEventListener("error", event => {
    if (event.target instanceof HTMLLinkElement && event.target.rel === "stylesheet") {
      document.documentElement.dataset.failedStylesheet = "1";
    }
  }, true);
  document.addEventListener("DOMContentLoaded", () => {
    const article = document.createElement("article");
    const section = document.createElement("section");
    section.className = "gdarticlebody";
    section.id = "gd-" + id;
    article.append(section);
    document.querySelector("main").append(article);
    // Reserve the same sidebar width as a normal result in the viewer.
    document.getElementById("viewer-dictionaries").hidden = false;
    renderFragment(section, restoreOrphanRows(html));
    for (const details of section.querySelectorAll("details")) details.open = true;
  });
}

function extractTextArticle(extract) {
  const root = document.querySelector("main > article > section");
  const warnings = new Set();
  const result = extract(root, generatedLabels(root, warnings));
  if (document.documentElement.dataset.failedStylesheet) warnings.add("stylesheet-unavailable");
  if (!result.text) warnings.add("empty-text");
  return JSON.stringify({text: result.text, warnings: [...result.warnings, ...warnings]});
}
