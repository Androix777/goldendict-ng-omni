const inversion = document.getElementById("viewer-inversion");
const inversionKey = "gd-viewer.inverted";
function setInversion(enabled) {
  inversion.checked = enabled;
  document.documentElement.classList.toggle("viewer-inverted", enabled);
}
function restoreInversion() {
  try { setInversion(localStorage.getItem(inversionKey) === "true"); } catch {}
}
restoreInversion();
inversion.addEventListener("change", () => {
  setInversion(inversion.checked);
  try { localStorage.setItem(inversionKey, String(inversion.checked)); } catch {}
});
window.addEventListener("pageshow", restoreInversion);
window.addEventListener("storage", event => {
  if (event.key === inversionKey || event.key === null) restoreInversion();
});

// Parse each dictionary fragment independently. A malformed closing tag in one
// entry must not swallow the search form or another dictionary's article.
const group = document.querySelector("select[name=group]").value;
const lookup = (word, anchor = "") => {
  const url = new URL("/lookup", location.origin);
  url.searchParams.set("word", word);
  url.searchParams.set("group", group);
  url.hash = anchor;
  return url.href;
};
function resourceUrls(text) {
  return text.replace(/(?:bres|gdau|gdpicture|gdvideo):\/\/([\w-]+)\//gi, "/resource/$1/")
             .replace(/qrcx?:\/{2,3}icons\//gi, "/icons/");
}
function adaptLink(a) {
  const raw = a.getAttribute("href");
  if (!raw || raw.startsWith("#")) return;
  if (/^gdau:\/\//i.test(raw)) {
    a.href = resourceUrls(raw);
    a.addEventListener("click", (event) => {
      event.preventDefault();
      player.src = a.href;
      player.hidden = false;
      player.play().catch(() => {});
    });
    return;
  }
  if (/^(gdlookup|bword|entry):/i.test(raw)) {
    const url = new URL(raw);
    const word = url.searchParams.get("word") || decodeURIComponent(url.pathname.replace(/^\//, "") || url.hostname);
    a.href = lookup(word, url.searchParams.get("gdanchor") || url.hash.slice(1));
  } else if (/^(?:bres|gdpicture|gdvideo):/i.test(raw)) {
    a.href = resourceUrls(raw);
  } else if (/^https?:\/\//i.test(raw)) {
    a.rel = "noreferrer";
  } else if (!/^(?:[\w+.-]+:|\/\/)/.test(raw)) {
    const [word, anchor] = raw.split("#");
    a.href = lookup(word, anchor);
  } else {
    a.removeAttribute("href");
  }
}
const player = document.querySelector("audio#pronunciation");
function renderFragment(section, html) {
  const template = document.createElement("template");
  template.innerHTML = html;
  const fragment = template.content;
  for (const el of fragment.querySelectorAll("script,iframe,frame,object,embed,base,meta,form,template,svg script")) el.remove();
  for (const el of fragment.querySelectorAll("*")) {
    for (const {name, value} of [...el.attributes]) {
      if (/^on/i.test(name) || ["srcdoc","autoplay","formaction","target","ping","nonce"].includes(name)) el.removeAttribute(name);
      else if (["src","srcset","style","poster","background"].includes(name)) el.setAttribute(name, resourceUrls(value));
    }
    if (el.localName === "style") el.textContent = resourceUrls(el.textContent);
    if (el.localName === "link") {
      if (el.rel === "stylesheet") el.setAttribute("href", resourceUrls(el.getAttribute("href") || ""));
      else el.remove();
    }
    if (el.localName === "a") {
      try { adaptLink(el); } catch { el.removeAttribute("href"); }
    }
  }
  section.append(fragment);
}
for (const section of document.querySelectorAll("[data-fragment]")) {
  const bytes = Uint8Array.from(atob(section.dataset.fragment), c => c.charCodeAt(0));
  delete section.dataset.fragment;
  renderFragment(section, new TextDecoder().decode(bytes));
}

function setupDictionaryNavigation() {
  const articles = [...document.querySelectorAll("#gd-viewer > main > article")];
  if (!articles.length) return;
  const nav = document.getElementById("viewer-dictionaries");
  const list = nav.querySelector("ul");
  const select = nav.querySelector("select");
  const links = articles.map(article => {
    const heading = article.firstElementChild;
    const name = heading.textContent;
    const link = document.createElement("a");
    link.textContent = name;
    const icon = heading.querySelector(".viewer-dict-icon");
    if (icon) link.prepend(icon.cloneNode());
    link.href = "#" + article.id;
    const item = document.createElement("li");
    item.append(link);
    list.append(item);
    select.add(new Option(name, article.id));
    return link;
  });
  nav.hidden = false;
  select.addEventListener("change", () => { location.hash = select.value; });

  let current = -1;
  function updateCurrent() {
    const top = Math.max(12, parseFloat(getComputedStyle(articles[0]).scrollMarginTop) + 2);
    let index = 0;
    for (let i = 1; i < articles.length; ++i) {
      if (articles[i].getBoundingClientRect().top > top) break;
      index = i;
    }
    // Short entries near the bottom cannot always scroll all the way to the top.
    if (window.scrollY + window.innerHeight >= document.documentElement.scrollHeight - 2) {
      const target = articles.findIndex(article => "#" + article.id === location.hash && article.getBoundingClientRect().top >= top - 2);
      if (target >= 0) index = target;
      else if (window.scrollY > 0) index = articles.length - 1;
    }
    if (current === index) return;
    if (current >= 0) links[current].removeAttribute("aria-current");
    current = index;
    const link = links[index];
    link.setAttribute("aria-current", "location");
    select.selectedIndex = index;
    const label = nav.querySelector("label");
    label.querySelector("img")?.remove();
    const icon = link.querySelector(".viewer-dict-icon");
    if (icon) label.prepend(icon.cloneNode());
    if (list.clientHeight) {
      const bounds = list.getBoundingClientRect();
      const row = link.getBoundingClientRect();
      if (row.top < bounds.top) list.scrollTop += row.top - bounds.top;
      else if (row.bottom > bounds.bottom) list.scrollTop += row.bottom - bounds.bottom;
    }
  }
  let scheduled = false;
  function scheduleUpdate() {
    if (scheduled) return;
    scheduled = true;
    requestAnimationFrame(() => { scheduled = false; updateCurrent(); });
  }
  window.addEventListener("scroll", scheduleUpdate, { passive: true });
  window.addEventListener("hashchange", scheduleUpdate);
  window.addEventListener("resize", scheduleUpdate);
  const observer = new ResizeObserver(scheduleUpdate);
  articles.forEach(article => observer.observe(article));
  updateCurrent();
}
setupDictionaryNavigation();
if (location.hash) {
  try { document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView(); } catch {}
}
