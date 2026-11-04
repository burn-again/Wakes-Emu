// md.js -- a small Markdown renderer for Wakes' reference docs (docs/*.md): headings,
// paragraphs, tables, lists, block quotes, code, emphasis and links. Enough for those
// files, with no library to load. Input is the repository's own docs.

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
	.replace(/"/g, '&quot;');

export function slug(s) {
	return s.toLowerCase().replace(/<[^>]+>/g, '').replace(/[^\w\s-]/g, '').trim().replace(/\s+/g, '-');
}

// opts.link(href) -> { href, doc } : where a link goes (doc = another doc's file name)
function inline(text, opts) {
	// Code spans become placeholders first, so nothing inside them is read as emphasis
	// or a link, while emphasis can still wrap one (**`••`**).
	const codes = [];
	const held = text.replace(/`+([^`]*)`+/g, (m, c) => {
		codes.push(`<code>${esc(c)}</code>`);
		return `\u0001${codes.length - 1}\u0002`;
	});
	return [held].map((p) => {
		let h = esc(p);
		h = h.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (m, label, href) => {
			const to = opts.link ? opts.link(href.replace(/&amp;/g, '&')) : { href };
			if (to.doc) {
				return `<a href="#" data-doc="${esc(to.doc)}" data-anchor="${esc(to.anchor || '')}">${label}</a>`;
			}
			if (to.anchor !== undefined && !to.href) {
				return `<a href="#" data-anchor="${esc(to.anchor)}">${label}</a>`;
			}
			return `<a href="${esc(to.href)}" target="_blank" rel="noopener">${label}</a>`;
		});
		h = h.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
		h = h.replace(/(^|[^\w*])\*([^*\s][^*]*?)\*(?!\w)/g, '$1<em>$2</em>');
		h = h.replace(/(^|[^\w])_([^_\s][^_]*?)_(?!\w)/g, '$1<em>$2</em>');
		return h.replace(/\u0001(\d+)\u0002/g, (m, n) => codes[Number(n)]);
	}).join('');
}

function splitRow(line) {
	let l = line.trim();
	if (l.startsWith('|')) { l = l.slice(1); }
	if (l.endsWith('|') && !l.endsWith('\\|')) { l = l.slice(0, -1); }
	// split on | not inside a code span
	const cells = [];
	let cur = '', inCode = false;
	for (const ch of l) {
		if (ch === '`') { inCode = !inCode; }
		if (ch === '|' && !inCode && !cur.endsWith('\\')) { cells.push(cur); cur = ''; continue; }
		cur += ch;
	}
	cells.push(cur);
	return cells.map((c) => c.trim().replace(/\\\|/g, '|'));   /* \| is a literal pipe */
}

const isSep = (l) => /^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$/.test(l);
const listRe = /^(\s*)([-*+]|\d+\.)\s+(.*)$/;

function renderList(lines, i, opts) {
	// Lists, nested by indentation.
	const out = [];
	const stack = [];
	let text = null;                 // the open item's text, rendered when it closes
	const flush = () => {
		if (text !== null) { out.push(inline(text, opts)); text = null; }
	};
	while (i < lines.length) {
		const m = listRe.exec(lines[i]);
		if (!m) {
			// continuation of the open item (indented text), else the list ends
			if (lines[i].trim() !== '' && /^\s{2,}/.test(lines[i]) && stack.length) {
				text += ' ' + lines[i].trim();
				i++;
				continue;
			}
			break;
		}
		flush();
		const indent = m[1].replace(/\t/g, '    ').length;
		const ordered = /\d/.test(m[2]);
		while (stack.length && indent < stack[stack.length - 1].indent) {
			out.push(`</li></${stack.pop().tag}>`);
		}
		if (!stack.length || indent > stack[stack.length - 1].indent) {
			const tag = ordered ? 'ol' : 'ul';
			stack.push({ indent, tag });
			out.push(`<${tag}><li>`);
		} else {
			out.push('</li><li>');
		}
		text = m[3];
		i++;
	}
	flush();
	while (stack.length) {
		out.push(`</li></${stack.pop().tag}>`);
	}
	return [out.join(''), i];
}

export function renderMarkdown(src, opts = {}) {
	const lines = src.replace(/\r\n?/g, '\n').split('\n');
	const out = [];
	let i = 0;
	while (i < lines.length) {
		const line = lines[i];
		if (line.trim() === '') { i++; continue; }
		const fence = /^\s*```/.exec(line);
		if (fence) {
			const body = [];
			i++;
			while (i < lines.length && !/^\s*```/.test(lines[i])) { body.push(lines[i++]); }
			i++;
			out.push(`<pre><code>${esc(body.join('\n'))}</code></pre>`);
			continue;
		}
		const h = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
		if (h) {
			const n = h[1].length;
			const html = inline(h[2], opts);
			out.push(`<h${n} id="${slug(h[2])}">${html}</h${n}>`);
			i++;
			continue;
		}
		if (/^\s*(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) { out.push('<hr>'); i++; continue; }
		if (line.trim().startsWith('|') && i + 1 < lines.length && isSep(lines[i + 1])) {
			const head = splitRow(line);
			i += 2;
			const rows = [];
			while (i < lines.length && lines[i].trim().startsWith('|')) { rows.push(splitRow(lines[i++])); }
			const th = head.map((c) => `<th>${inline(c, opts)}</th>`).join('');
			const tb = rows.map((r) => `<tr>${r.map((c) => `<td>${inline(c, opts)}</td>`).join('')}</tr>`).join('');
			const headRow = head.some((c) => c !== '') ? `<thead><tr>${th}</tr></thead>` : '';
			out.push(`<div class="tbl"><table>${headRow}<tbody>${tb}</tbody></table></div>`);
			continue;
		}
		if (/^\s*>/.test(line)) {
			const body = [];
			while (i < lines.length && /^\s*>/.test(lines[i])) { body.push(lines[i++].replace(/^\s*>\s?/, '')); }
			out.push(`<blockquote>${renderMarkdown(body.join('\n'), opts)}</blockquote>`);
			continue;
		}
		if (listRe.test(line)) {
			const [html, next] = renderList(lines, i, opts);
			out.push(html);
			i = next;
			continue;
		}
		const para = [];
		while (i < lines.length && lines[i].trim() !== '' && !/^\s*(```|#{1,6}\s|>)/.test(lines[i]) &&
		       !listRe.test(lines[i]) && !(lines[i].trim().startsWith('|') && i + 1 < lines.length && isSep(lines[i + 1]))) {
			para.push(lines[i++].trim());
		}
		out.push(`<p>${inline(para.join(' '), opts)}</p>`);
	}
	return out.join('\n');
}
