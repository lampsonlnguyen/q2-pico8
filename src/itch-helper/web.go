// SPDX-License-Identifier: GPL-2.0-or-later
// Browser-only PICO-8 projects: most PICO-8 games on itch.io are uploaded
// only as PICO-8's web export, whose JavaScript carries the cartridge ROM
// as `var _cartdat=[...]` (one 0x8000-byte ROM per cart, the main cart
// first). FAKE-08 cannot load a raw ROM, so the ROM is stored in a
// standard 160x205 .p8.png: each byte in the two low bits of the A, R, G
// and B channels, as PICO-8 itself saves PNG carts.
package main

import (
	"bytes"
	"errors"
	"image"
	"image/color"
	"image/png"
	"net/http"
	"net/url"
	"regexp"

	"golang.org/x/net/html"
)

const romSize = 0x8000
const codeStart = 0x4300

var (
	embedTag  = regexp.MustCompile(`https://[a-z0-9.-]+/html/[0-9]+/[^"'<>\s?#]*\.html?`)
	scriptTag = regexp.MustCompile(`(?i)<script[^>]+src\s*=\s*["']([^"']+\.js)["']`)
	errNoWeb  = errors.New("No PICO-8 web cart on this page")
)

// embedURL finds the project's embedded HTML game, if any. itch.io puts it
// in an iframe, or HTML-escaped in a data-iframe placeholder.
func embedURL(doc []byte) string {
	text := html.UnescapeString(string(doc))
	for _, match := range embedTag.FindAllString(text, 8) {
		if u, err := url.Parse(match); err == nil && permitted(u) {
			return match
		}
	}
	return ""
}

// parseCartdat reads the first cart ROM from a PICO-8 web export script.
func parseCartdat(js []byte) ([]byte, error) {
	at := bytes.Index(js, []byte("_cartdat"))
	if at < 0 {
		return nil, errNoWeb
	}
	rest := js[at+len("_cartdat"):]
	open := bytes.IndexByte(rest, '[')
	if open < 0 || open > 16 || bytes.IndexByte(rest[:open], '=') < 0 {
		return nil, errNoWeb
	}
	rom := make([]byte, 0, romSize)
	value, digits := 0, 0
	for _, ch := range rest[open+1:] {
		switch {
		case ch >= '0' && ch <= '9':
			value = value*10 + int(ch-'0')
			if digits++; digits > 3 || value > 255 {
				return nil, errNoWeb
			}
		case ch == ',' || ch == ']':
			if digits == 0 && ch == ',' {
				return nil, errNoWeb
			}
			if digits > 0 && len(rom) < romSize {
				rom = append(rom, byte(value))
			}
			value, digits = 0, 0
			if ch == ']' {
				return validROM(rom)
			}
		case ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t':
			if digits > 0 && len(rom) < romSize {
				rom = append(rom, byte(value))
				value, digits = 0, 0
			}
		default:
			return nil, errNoWeb
		}
	}
	return nil, errNoWeb
}

// validROM accepts a ROM whose code section is PICO-8's old (":c:\0") or
// new ("\0pxa") compressed format, or plain Lua text.
func validROM(rom []byte) ([]byte, error) {
	if len(rom) < codeStart+8 {
		return nil, errNoWeb
	}
	for len(rom) < romSize {
		rom = append(rom, 0)
	}
	code := rom[codeStart:]
	if bytes.HasPrefix(code, []byte(":c:\x00")) || bytes.HasPrefix(code, []byte("\x00pxa")) {
		return rom, nil
	}
	n := 0
	for n < len(code) && code[n] != 0 {
		if c := code[n]; c < 9 || (c > 13 && c < 32) || c > 126 {
			if c < 128 { // PICO-8 glyphs use 128..255; control bytes do not appear
				return nil, errNoWeb
			}
		}
		n++
	}
	if n == 0 {
		return nil, errNoWeb
	}
	return rom, nil
}

// encodeCartPNG stores rom in a 160x205 PNG cart. The visible picture is a
// plain cart body with the itch.io cover, when there is one, as its label.
func encodeCartPNG(rom []byte, art image.Image) []byte {
	const w, h = 160, 205
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	body := color.NRGBA{0x2a, 0x2d, 0x3a, 0xff}
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.SetNRGBA(x, y, body)
		}
	}
	for y := 20; y < 156; y++ {
		for x := 12; x < 148; x++ {
			img.SetNRGBA(x, y, color.NRGBA{0x10, 0x10, 0x14, 0xff})
		}
	}
	if art != nil {
		b := art.Bounds()
		scale := float64(128) / float64(b.Dx())
		if s := float64(128) / float64(b.Dy()); s < scale {
			scale = s
		}
		dw, dh := int(float64(b.Dx())*scale), int(float64(b.Dy())*scale)
		ox, oy := 16+(128-dw)/2, 24+(128-dh)/2
		for y := 0; y < dh; y++ {
			for x := 0; x < dw; x++ {
				sx := b.Min.X + int(float64(x)/scale)
				sy := b.Min.Y + int(float64(y)/scale)
				c := color.NRGBAModel.Convert(art.At(sx, sy)).(color.NRGBA)
				c.A = 0xff
				img.SetNRGBA(ox+x, oy+y, c)
			}
		}
	}
	for i := 0; i < w*h; i++ {
		var b byte
		if i < len(rom) && i < romSize {
			b = rom[i]
		}
		p := img.Pix[i*4 : i*4+4]
		p[0] = p[0]&^3 | b>>4&3
		p[1] = p[1]&^3 | b>>2&3
		p[2] = p[2]&^3 | b&3
		p[3] = p[3]&^3 | b>>6&3
	}
	var out bytes.Buffer
	_ = png.Encode(&out, img)
	return out.Bytes()
}

// decodeCartPNG is the inverse, as FAKE-08 reads PNG carts (tests only).
func decodeCartPNG(data []byte) ([]byte, error) {
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	nrgba, ok := img.(*image.NRGBA)
	if !ok || nrgba.Rect.Dx() != 160 || nrgba.Rect.Dy() != 205 {
		return nil, errNoWeb
	}
	out := make([]byte, romSize)
	for i := range out {
		p := nrgba.Pix[i*4 : i*4+4]
		out[i] = p[3]&3<<6 | p[0]&3<<4 | p[1]&3<<2 | p[2]&3
	}
	return out, nil
}

func webScripts(c *http.Client, index string, doc []byte) ([]byte, error) {
	if bytes.Contains(doc, []byte("_cartdat")) {
		return doc, nil
	}
	base, err := url.Parse(index)
	if err != nil {
		return nil, errNoWeb
	}
	for _, m := range scriptTag.FindAllSubmatch(doc, 4) {
		ref, err := url.Parse(html.UnescapeString(string(m[1])))
		if err != nil {
			continue
		}
		src := base.ResolveReference(ref)
		if !permitted(src) {
			continue
		}
		js, err := pageLimit(c, src.String(), nil, index, maxScript)
		if err != nil {
			if errors.Is(err, errConnect) {
				return nil, err
			}
			continue
		}
		if bytes.Contains(js, []byte("_cartdat")) {
			return js, nil
		}
	}
	return nil, errNoWeb
}

// webCart fetches the cart ROM from the project's embedded web export.
func webCart(c *http.Client, base string) ([]byte, error) {
	doc, err := page(c, base, nil, "")
	if err != nil {
		return nil, err
	}
	index := embedURL(doc)
	if index == "" {
		return nil, errNoWeb
	}
	game, err := pageLimit(c, index, nil, base, maxPage)
	if err != nil {
		return nil, err
	}
	js, err := webScripts(c, index, game)
	if err != nil {
		return nil, err
	}
	return parseCartdat(js)
}
