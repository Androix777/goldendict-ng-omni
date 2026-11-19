// Operates on a styled article in a disposable browser page.
(root, generated) => {
  const doc = root.ownerDocument;
  const warnings = new Set();
  const visibleText = root.innerText.trim();
  const isShown = el => el.checkVisibility ? el.checkVisibility({visibilityProperty: true}) :
    el.getClientRects().length > 0 && getComputedStyle(el).visibility === "visible";
  const elements = [...root.querySelectorAll("*")];

  // Read spacing before changing the DOM. CSS margins can separate an example
  // from its translation even when the HTML has no intervening whitespace.
  const separated = [];
  for (const el of elements) {
    const next = el.nextSibling;
    if (next?.nodeType !== Node.ELEMENT_NODE || !isShown(el) || !isShown(next)) continue;
    const left = getComputedStyle(el), right = getComputedStyle(next);
    const parent = getComputedStyle(el.parentElement);
    const flexRow = parent.display.includes("flex") && parent.flexDirection === "row";
    if (!flexRow && (!left.display.startsWith("inline") || !right.display.startsWith("inline"))) continue;
    const gap = [left.paddingInlineEnd, left.marginInlineEnd, right.paddingInlineStart,
      right.marginInlineStart, /flex|grid/.test(parent.display) ? parent.columnGap : "0"]
      .reduce((total, value) => total + (parseFloat(value) || 0), 0);
    if (gap <= 0) continue;
    const a = el.innerText || "", b = next.innerText || "";
    if (!a || !b || /\s$/u.test(a) || /^\s/u.test(b)) continue;
    const cjk = /[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}]/u;
    const switchesScript = (cjk.test(a) && /^\p{Script=Latin}/u.test(b)) ||
      (cjk.test(b) && /\p{Script=Latin}[\p{Punctuation}]*$/u.test(a) && !cjk.test(a));
    if (switchesScript && gap >= parseFloat(right.fontSize) * 0.75 && Math.min(a.length, b.length) >= 4)
      separated.push([el, doc.createElement("br")]);
    else if (switchesScript || (/[a-z0-9]$/i.test(a) && /^[a-z0-9]/i.test(b)))
      separated.push([el, doc.createTextNode(" ")]);
  }
  for (const [el, separator] of separated) el.after(separator);

  // innerText excludes pseudo-elements. Insert their supplied labels before
  // reading the text, including CSS counters and list markers.
  for (const [id, parts] of Object.entries(generated)) {
    const el = elements[Number(id)];
    if (!el || !isShown(el)) continue;
    if (parts.before) el.prepend(doc.createTextNode(parts.before));
    if (parts.after) el.append(doc.createTextNode(parts.after));
    if (parts.marker) el.prepend(doc.createTextNode(parts.marker.trimEnd() + " "));
  }

  // Measure all images in the original layout: replacing an image can resize
  // its neighbours (e.g. a row of illustrations in a flex container).
  let largeImages = 0, imageAreaEm = 0;
  const images = [];
  for (const img of root.querySelectorAll("img")) {
    if (!isShown(img)) continue;
    const alt = img.alt.trim();
    const label = [alt, img.getAttribute("aria-label"), img.title].find(value =>
      value?.trim() && !/\.(svg|png|jpe?g|gif|webp)$/i.test(value.trim()));
    const audio = img.closest("a")?.getAttribute("href") || "";
    if (/\.(wav|mp3|ogg|opus|m4a|flac|spx)([?#]|$)/i.test(audio) ||
        img.closest('[aria-hidden="true"]') || /^(none|presentation)$/.test(img.getAttribute("role"))) continue;
    const box = img.getBoundingClientRect();
    const em = parseFloat(getComputedStyle(img).fontSize) || 16;
    if (Math.min(box.height, box.width) >= 4 * em) {
      largeImages++;
      imageAreaEm += box.width * box.height / (em * em);
    }
    if (label) images.push([img, label === img.title && !alt ? "〔" + label.trim() + "〕" : label.trim()]);
    else {
      warnings.add("image-without-text");
      if (box.height >= 64) images.push([img, "[image]"]);
      else if (!img.hasAttribute("alt") && box.height >= em * 0.5 && box.width >= em * 0.5 &&
               /\S$/u.test(img.previousSibling?.textContent || "") &&
               /^\S/u.test(img.nextSibling?.textContent || ""))
        images.push([img, "[glyph]"]);
    }
  }
  for (const [img, replacement] of images) img.replaceWith(doc.createTextNode(replacement));
  // Keep reading annotations even when the dictionary hides rt in its default view.
  for (const ruby of [...root.querySelectorAll("ruby")].reverse()) {
    let text = "", bases = [], readings = [];
    function flush() {
      const base = bases.join("");
      if (base && bases.length === readings.length) {
        text += bases.map((b, i) => annotate(b, readings[i])).join("");
      } else {
        text += annotate(base, readings.join("、"));
      }
      bases = []; readings = [];
    }
    function annotate(base, reading) {
      return reading && reading !== base ? base + "（" + reading + "）" : base;
    }
    for (const node of ruby.childNodes) {
      if (node.localName === "rp") continue;
      const value = node.textContent.trim();
      if (!value || node.nodeType === Node.COMMENT_NODE) continue;
      if (node.localName === "rt" || node.localName === "rtc") readings.push(value);
      else {
        if (readings.length) flush();
        bases.push(value);
      }
    }
    flush();
    ruby.textContent = text;
  }
  for (const el of root.querySelectorAll("button,input,select,textarea,audio,video")) el.remove();
  for (const heading of root.querySelectorAll("h1,h2,h3,h4,h5,h6")) {
    if (heading.innerText.trim()) heading.prepend(doc.createTextNode("#".repeat(Number(heading.tagName[1])) + " "));
  }
  const text = root.innerText.replace(/\u00a0/g, " ").replace(/[ \t]+\n/g, "\n").replace(/\n{3,}/g, "\n\n").trim();
  // A short caption (even a nonempty alt) may accompany an entire scanned
  // page. Keep all available text and signal when the visual content dominates.
  const characters = text.replace(/\[image\]|\s/gu, "").length;
  if (largeImages && characters <= 40 * largeImages && imageAreaEm >= Math.max(32, 3 * characters))
    warnings.add("primarily-visual");
  return {text, visibleText, warnings: [...warnings]};
}
