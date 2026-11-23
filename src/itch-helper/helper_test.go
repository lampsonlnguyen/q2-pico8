// SPDX-License-Identifier: GPL-2.0-or-later
package main

import (
	"archive/zip"
	"bytes"
	"fmt"
	"image"
	"image/color"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestCatalogEntriesAndQuery(t *testing.T) {
	raw := []byte(`<link rel="next" href="?page=2"><div class="browse_game_grid">
		<div class="game_cell" data-game_id="42">
		<a href="https://dev-name.itch.io/celeste-classic" class="title game_link">Celeste Classic</a>
		<div class="game_author"><a>Maddy &amp; Noel</a><svg><title>Verified Account</title></svg></div>
		<div class="price_value">$0</div><div class="game_genre">Platformer</div>
		</div>
		<div class="game_cell" data-game_id="43">
		<a href="https://evil.example.com/x" class="title game_link">Elsewhere</a></div>
		</div>`)
	games, more, err := catalogEntries(raw)
	if err != nil || !more || len(games) != 1 {
		t.Fatalf("games=%v more=%v err=%v", games, more, err)
	}
	g := games[0]
	if g.id != 42 || g.creator != "Maddy & Noel" || !isFree(g.price) ||
		!matchesQuery(g, "celeste maddy") || !matchesQuery(g, "dev name") ||
		matchesQuery(g, "verified") {
		t.Fatalf("incorrect entry or search for %+v", g)
	}
}

func TestCatalogURL(t *testing.T) {
	for order, want := range map[string]string{
		"popular":   "https://itch.io/games/made-with-pico-8?page=1",
		"newest":    "https://itch.io/games/newest/made-with-pico-8?page=1",
		"top-rated": "https://itch.io/games/top-rated/made-with-pico-8?page=1",
	} {
		if got, ok := catalogURL(order, 1); !ok || got != want {
			t.Errorf("%s: %q, want %q", order, got, want)
		}
	}
	if _, ok := catalogURL("random", 1); ok {
		t.Error("accepted an unknown sort")
	}
}

type roundTrip func(*http.Request) (*http.Response, error)

func (fn roundTrip) RoundTrip(req *http.Request) (*http.Response, error) { return fn(req) }

func zipWith(t *testing.T, member string, content []byte) []byte {
	var body bytes.Buffer
	zw := zip.NewWriter(&body)
	w, err := zw.Create(member)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = w.Write(content); err != nil {
		t.Fatal(err)
	}
	if err = zw.Close(); err != nil {
		t.Fatal(err)
	}
	return body.Bytes()
}

func TestArchiveVerification(t *testing.T) {
	cart := []byte("pico-8 cartridge // http://www.pico-8.com\nversion 41\n__lua__\nprint(1)\n")
	for _, test := range []struct {
		member string
		want   bool
	}{{"game/celeste.p8", true}, {"celeste.P8.PNG", true}, {"index.html", false},
		{"__MACOSX/._x.p8", false}, {"game.exe", false}} {
		body := zipWith(t, test.member, cart)
		size := len(body)
		client := &http.Client{Transport: roundTrip(func(req *http.Request) (*http.Response, error) {
			if req.Header.Get("Range") == "" {
				t.Error("expected a bounded range")
			}
			headers := make(http.Header)
			headers.Set("Content-Range", fmt.Sprintf("bytes 0-%d/%d", size-1, size))
			return &http.Response{StatusCode: http.StatusPartialContent, Header: headers,
				Body: io.NopCloser(bytes.NewReader(body))}, nil
		})}
		if got := archiveHasCart(client, "https://files.itch.zone/sample.zip"); got != test.want {
			t.Errorf("%q verified=%v, want %v", test.member, got, test.want)
		}
	}
}

func TestCartKindAndValidation(t *testing.T) {
	if cartKind("Game.P8.png") != ".p8.png" || cartKind("x.p8") != ".p8" ||
		cartKind("web.zip") != ".zip" || cartKind("game.html") != "" {
		t.Error("bad classification")
	}
	if !validCart([]byte("pico-8 cartridge // x"), ".p8") || validCart([]byte("<html>"), ".p8") {
		t.Error("bad .p8 validation")
	}
	png := encodeCartPNG(sampleROM(), nil)
	if !validCart(png, ".p8.png") {
		t.Error("encoded cart is not a 160x205 PNG")
	}
	if titleStem(`Celeste: "Classic".p8.png`, 7) != "Celeste Classic [itch 7]" {
		t.Errorf("stem %q", titleStem(`Celeste: "Classic".p8.png`, 7))
	}
}

func sampleROM() []byte {
	rom := make([]byte, romSize)
	for i := 0; i < codeStart; i++ {
		rom[i] = byte(i * 7)
	}
	copy(rom[codeStart:], "-- hello\nprint('hi')\n")
	return rom
}

func TestWebExportRoundTrip(t *testing.T) {
	rom := sampleROM()
	var js strings.Builder
	js.WriteString("var _cartname=['hello.p8'];\nvar _cartdat=[")
	for i, b := range rom[:codeStart+64] {
		if i > 0 {
			js.WriteString(",")
		}
		if i%40 == 0 {
			js.WriteString("\n")
		}
		fmt.Fprintf(&js, "%d", b)
	}
	js.WriteString("];\nfunction x(){}")
	got, err := parseCartdat([]byte(js.String()))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, rom) {
		t.Fatal("parsed ROM differs")
	}
	art := image.NewRGBA(image.Rect(0, 0, 315, 250))
	for i := range art.Pix {
		art.Pix[i] = 0xff
	}
	art.Set(10, 10, color.RGBA{1, 2, 3, 255})
	decoded, err := decodeCartPNG(encodeCartPNG(got, art))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(decoded, rom) {
		t.Fatal("PNG cart does not round-trip the ROM")
	}
	for _, bad := range []string{"var _cartdat=[1,2,3];", "var _cartdat=[999];", "_cartdat=[1,,2]", "nothing"} {
		if _, err := parseCartdat([]byte(bad)); err == nil {
			t.Errorf("accepted %q", bad)
		}
	}
	binary := append(make([]byte, codeStart), 1, 2, 3, 4, 5, 6, 7, 8)
	if _, err := validROM(binary); err == nil {
		t.Error("accepted control bytes as Lua")
	}
	compressed := append(make([]byte, codeStart), []byte("\x00pxa\x00\x10\x00\x08")...)
	if _, err := validROM(compressed); err != nil {
		t.Error("rejected a pxa ROM")
	}
}

func TestEmbedURL(t *testing.T) {
	doc := `<div class="iframe_placeholder" data-iframe="&lt;iframe src=&quot;https://html-classic.itch.zone/html/123456/index.html&quot; allowfullscreen&gt;&lt;/iframe&gt;"></div>`
	if got := embedURL([]byte(doc)); got != "https://html-classic.itch.zone/html/123456/index.html" {
		t.Errorf("embed %q", got)
	}
	if embedURL([]byte(`<iframe src="https://evil.example.com/html/1/index.html">`)) != "" {
		t.Error("accepted an embed outside itch.io")
	}
}

func TestCartFromArchive(t *testing.T) {
	cart := []byte("pico-8 cartridge // http://www.pico-8.com\nversion 41\n__lua__\n")
	dir := t.TempDir()
	path := filepath.Join(dir, "a.zip")
	if err := os.WriteFile(path, zipWith(t, "bundle/demo.p8", cart), 0o644); err != nil {
		t.Fatal(err)
	}
	f, _ := os.Open(path)
	defer f.Close()
	st, _ := f.Stat()
	data, ext, err := cartFrom(f, st.Size(), ".zip")
	if err != nil || ext != ".p8" || !bytes.Equal(data, cart) {
		t.Fatalf("ext=%q err=%v", ext, err)
	}
	if err := install(dir, "x.p8", bytes.NewReader(data), maxCart); err != nil {
		t.Fatal(err)
	}
	if err := install(dir, "x.p8", bytes.NewReader(data), maxCart); err != errExists {
		t.Fatalf("second install: %v", err)
	}
}
