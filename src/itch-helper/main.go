// SPDX-License-Identifier: GPL-2.0-or-later
// itch.io helper for the Q2 PICO-8 player. Adapted from Pocket Boy's
// q2-boy-itch (github.com/SlayTheEarth/q2-boy, GPL-2.0-or-later), whose
// guest download flow and upload markup were in turn adapted from
// NextUI-Itchio-Pak (MIT). See NextUI-Itchio-Pak-LICENSE.
//
// Commands (results on stdout; on failure one line of text, exit 1):
//
//	list <all|free|paid> <popular|newest|top-rated> <page> <query>
//	download <project-url> <cart-dir> <cover-dir>
//	qr <project-url>
package main

import (
	"archive/zip"
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	"io"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/skip2/go-qrcode"
	_ "golang.org/x/image/webp"
	"golang.org/x/net/html"
)

const maxCart = 4 << 20
const maxArchive = 48 << 20
const maxPage = 2 << 20
const maxScript = 16 << 20
const agent = "q2-pico8/0.4 (https://github.com/lampsonlnguyen/q2-pico8)"

var (
	errVisit   = errors.New("Open the itch.io page for this download")
	errNoCart  = errors.New("No PICO-8 cart in the free files")
	errConnect = errors.New("Could not reach itch.io. Check Q2 Wi-Fi")
	errExists  = errors.New("This cart is already in your library")
	gameSlug   = regexp.MustCompile(`^/[a-zA-Z0-9_-]+/?$`)
	csrfTag    = regexp.MustCompile(`name="csrf_token"\s+(?:content|value)="([^"]+)"`)
)

func permitted(u *url.URL) bool {
	if u == nil || u.Scheme != "https" || u.User != nil || u.Port() != "" {
		return false
	}
	h := u.Hostname()
	return h == "itch.io" || strings.HasSuffix(h, ".itch.io") ||
		h == "itch.zone" || strings.HasSuffix(h, ".itch.zone") ||
		h == "hwcdn.net" || strings.HasSuffix(h, ".hwcdn.net") ||
		(strings.HasPrefix(h, "itchio-") && strings.HasSuffix(h, ".r2.cloudflarestorage.com"))
}

func project(raw string) (*url.URL, bool) {
	u, err := url.Parse(raw)
	return u, err == nil && permitted(u) && strings.HasSuffix(u.Hostname(), ".itch.io") &&
		gameSlug.MatchString(u.Path) && u.RawQuery == "" && u.Fragment == ""
}

func newClient() *http.Client {
	jar, _ := cookiejar.New(nil)
	return &http.Client{Jar: jar, Timeout: 18 * time.Second, CheckRedirect: func(r *http.Request, via []*http.Request) error {
		if len(via) >= 5 || !permitted(r.URL) {
			return errVisit
		}
		return nil
	}}
}

func fetch(c *http.Client, address string, form url.Values, referer string) (*http.Response, error) {
	method := http.MethodGet
	var body io.Reader
	if form != nil {
		method = http.MethodPost
		body = strings.NewReader(form.Encode())
	}
	req, err := http.NewRequest(method, address, body)
	if err != nil || !permitted(req.URL) {
		return nil, errVisit
	}
	req.Header.Set("User-Agent", agent)
	if form != nil || referer != "" {
		req.Header.Set("Cache-Control", "no-cache")
	}
	if referer != "" {
		req.Header.Set("Referer", referer)
	}
	if form != nil {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
		req.Header.Set("X-Requested-With", "XMLHttpRequest")
		req.Header.Set("Origin", req.URL.Scheme+"://"+req.URL.Host)
	}
	res, err := c.Do(req)
	if err != nil {
		if errors.Is(err, errVisit) {
			return nil, errVisit
		}
		return nil, errConnect
	}
	if res.StatusCode != http.StatusOK {
		res.Body.Close()
		if res.StatusCode == 429 || res.StatusCode >= 500 {
			return nil, errors.New("itch.io is busy. Try again later")
		}
		return nil, errVisit
	}
	return res, nil
}

func limited(r io.Reader, limit int64) ([]byte, error) {
	b, err := io.ReadAll(io.LimitReader(r, limit+1))
	if err != nil {
		return nil, errConnect
	}
	if int64(len(b)) > limit {
		return nil, errors.New("This file is too large for the Q2")
	}
	return b, nil
}

func pageLimit(c *http.Client, address string, form url.Values, referer string, limit int64) ([]byte, error) {
	res, err := fetch(c, address, form, referer)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	return limited(res.Body, limit)
}

func page(c *http.Client, address string, form url.Values, referer string) ([]byte, error) {
	return pageLimit(c, address, form, referer, maxPage)
}

func clean(s string, max int) string {
	s = strings.Join(strings.Fields(s), " ")
	if len(s) <= max {
		return s
	}
	for !utf8.RuneStart(s[max]) {
		max--
	}
	return s[:max]
}

func priceLabel(raw string) string {
	s := clean(raw, 26)
	if s == "" || s == "Free" {
		return "Free"
	}
	value := strings.Trim(s, "$€£¥ ")
	if n, err := strconv.ParseFloat(value, 64); err == nil && n == 0 {
		return "Free / name your price"
	}
	return s
}

func isFree(price string) bool {
	label := priceLabel(price)
	return label == "Free" || label == "Free / name your price"
}

type gameData struct {
	ID        int64  `json:"id"`
	Title     string `json:"title"`
	Price     string `json:"price"`
	Suggested string `json:"suggested_price"`
	Cover     string `json:"cover_image"`
}

func metadata(c *http.Client, base string) (gameData, error) {
	var data gameData
	raw, err := page(c, base+"/data.json", nil, "")
	if err != nil {
		return data, err
	}
	if json.Unmarshal(raw, &data) != nil || data.ID <= 0 || clean(data.Title, 95) == "" {
		return data, errVisit
	}
	return data, nil
}

type upload struct{ id, name string }

func uploads(doc []byte) []upload {
	root, err := html.Parse(bytes.NewReader(doc))
	if err != nil {
		return nil
	}
	var out []upload
	var walk func(*html.Node, *upload)
	walk = func(n *html.Node, item *upload) {
		var entry upload
		outer := n.Type == html.ElementNode && n.Data == "div" && nodeClass(n, "upload")
		if outer {
			item = &entry
		}
		if item != nil && n.Type == html.ElementNode {
			if n.Data == "a" {
				if id, err := strconv.ParseUint(nodeAttr(n, "data-upload_id"), 10, 64); err == nil && id != 0 {
					item.id = strconv.FormatUint(id, 10)
				}
			}
			if n.Data == "strong" && nodeClass(n, "name") {
				item.name = nodeAttr(n, "title")
				if item.name == "" && n.FirstChild != nil {
					item.name = strings.TrimSpace(n.FirstChild.Data)
				}
			}
		}
		for child := n.FirstChild; child != nil; child = child.NextSibling {
			walk(child, item)
		}
		if outer && entry.name != "" {
			out = append(out, entry)
		}
	}
	walk(root, nil)
	return out
}

func token(b []byte) string {
	if m := csrfTag.FindSubmatch(b); len(m) == 2 {
		return string(m[1])
	}
	return ""
}

func postURL(c *http.Client, address, referer string, values url.Values) (string, error) {
	var reply struct {
		URL string `json:"url"`
	}
	raw, err := page(c, address, values, referer)
	if err != nil {
		return "", err
	}
	if json.Unmarshal(raw, &reply) != nil || reply.URL == "" {
		return "", errVisit
	}
	u, err := url.Parse(reply.URL)
	if err != nil || !permitted(u) {
		return "", errVisit
	}
	return reply.URL, nil
}

func keyID(key string) string {
	payload, _, found := strings.Cut(key, ".")
	if !found {
		return ""
	}
	b, err := base64.RawStdEncoding.DecodeString(strings.TrimRight(payload, "="))
	if err != nil {
		b, err = base64.RawURLEncoding.DecodeString(strings.TrimRight(payload, "="))
	}
	var keyData struct {
		ID int64 `json:"id"`
	}
	if err != nil || json.Unmarshal(b, &keyData) != nil || keyData.ID <= 0 {
		return ""
	}
	return strconv.FormatInt(keyData.ID, 10)
}

func guestUploads(c *http.Client, base string) ([]upload, string, string, string, error) {
	// This page supplies a per-visit cookie and its CSRF token. A cached
	// listing page can omit the cookie even if it contains a token.
	session := fmt.Sprintf("%s?q2_pico8_session=%d", base, time.Now().UnixNano())
	doc, err := page(c, session, nil, base)
	if err != nil {
		return nil, "", "", "", err
	}
	csrf := token(doc)
	if csrf == "" {
		return nil, "", "", "", errVisit
	}
	signed, err := postURL(c, base+"/download_url", base,
		url.Values{"csrf_token": {csrf}, "suggested_amount": {"0"}})
	if err != nil {
		return nil, "", "", "", err
	}
	u, _ := url.Parse(signed)
	baseURL, _ := url.Parse(base)
	prefix := baseURL.EscapedPath() + "/download/"
	if u.Host != baseURL.Host || !strings.HasPrefix(u.EscapedPath(), prefix) {
		return nil, "", "", "", errVisit
	}
	key, err := url.PathUnescape(strings.TrimPrefix(u.EscapedPath(), prefix))
	if err != nil || key == "" {
		return nil, "", "", "", errVisit
	}
	doc, err = page(c, signed, nil, base)
	if err != nil {
		return nil, "", "", "", err
	}
	csrf = token(doc)
	if csrf == "" {
		return nil, "", "", "", errVisit
	}
	return uploads(doc), signed, csrf, keyID(key), nil
}

// cartKind classifies a file name: ".p8.png", ".p8", ".zip" or "".
func cartKind(name string) string {
	lower := strings.ToLower(strings.TrimSpace(name))
	switch {
	case strings.HasSuffix(lower, ".p8.png"):
		return ".p8.png"
	case strings.HasSuffix(lower, ".p8"):
		return ".p8"
	case strings.HasSuffix(lower, ".zip"):
		return ".zip"
	}
	return ""
}

// validCart checks the header of a cart FAKE-08 can load: a text cart, or
// a 160x205 PNG cart.
func validCart(data []byte, kind string) bool {
	switch kind {
	case ".p8":
		return bytes.HasPrefix(data, []byte("pico-8 cartridge"))
	case ".p8.png":
		return len(data) >= 24 && bytes.Equal(data[:8], []byte("\x89PNG\r\n\x1a\n")) &&
			bytes.Equal(data[12:16], []byte("IHDR")) &&
			bytes.Equal(data[16:24], []byte{0, 0, 0, 160, 0, 0, 0, 205})
	}
	return false
}

func readCart(r io.Reader, kind string) ([]byte, error) {
	data, err := io.ReadAll(io.LimitReader(r, maxCart+1))
	if err != nil || len(data) > maxCart || !validCart(data, kind) {
		return nil, errNoCart
	}
	return data, nil
}

func archiveCandidates(files []*zip.File) []*zip.File {
	var out []*zip.File
	for _, f := range files {
		name := path.Base(f.Name)
		kind := cartKind(name)
		if (kind != ".p8" && kind != ".p8.png") || strings.HasPrefix(name, ".") ||
			strings.HasPrefix(f.Name, "__MACOSX/") || !f.Mode().IsRegular() ||
			f.UncompressedSize64 < 16 || f.UncompressedSize64 > maxCart {
			continue
		}
		out = append(out, f)
	}
	// PNG carts carry their label art; prefer them, then the shallowest path.
	sort.SliceStable(out, func(i, j int) bool {
		pi, pj := cartKind(out[i].Name) == ".p8.png", cartKind(out[j].Name) == ".p8.png"
		if pi != pj {
			return pi
		}
		return strings.Count(out[i].Name, "/") < strings.Count(out[j].Name, "/")
	})
	return out
}

// cartFrom returns a validated cart from a staged download: the file itself
// or the best cart inside a ZIP archive.
func cartFrom(file *os.File, size int64, kind string) ([]byte, string, error) {
	if kind == ".p8" || kind == ".p8.png" {
		if data, err := readCart(file, kind); err == nil {
			return data, kind, nil
		}
		if _, err := file.Seek(0, io.SeekStart); err != nil {
			return nil, "", errNoCart
		}
	}
	z, err := zip.NewReader(file, size)
	if err != nil {
		return nil, "", errNoCart
	}
	for _, f := range archiveCandidates(z.File) {
		r, err := f.Open()
		if err != nil {
			continue
		}
		ext := cartKind(f.Name)
		data, err := readCart(r, ext)
		r.Close()
		if err == nil {
			return data, ext, nil
		}
	}
	return nil, "", errNoCart
}

func stageDownload(c *http.Client, address, dir string) (*os.File, int64, error) {
	res, err := fetch(c, address, nil, "")
	if err != nil {
		return nil, 0, err
	}
	defer res.Body.Close()
	if res.ContentLength > maxArchive {
		return nil, 0, errors.New("This download is too large for the Q2")
	}
	file, err := os.CreateTemp(dir, ".q2-pico8-download-*")
	if err != nil {
		return nil, 0, errors.New("Could not write to the SD card")
	}
	// An unlinked work file is reclaimed even when firmware closes the app.
	if err := os.Remove(file.Name()); err != nil {
		defer os.Remove(file.Name())
	}
	size, err := io.Copy(file, io.LimitReader(res.Body, maxArchive+1))
	if err != nil || size > maxArchive {
		file.Close()
		return nil, 0, errors.New("Download incomplete or too large")
	}
	if _, err := file.Seek(0, io.SeekStart); err != nil {
		file.Close()
		return nil, 0, errors.New("Could not read the staged download")
	}
	return file, size, nil
}

func titleStem(title string, id int64) string {
	stem := strings.Map(func(r rune) rune {
		if r < 32 || strings.ContainsRune(`<>:"/\|?*`, r) {
			return ' '
		}
		return r
	}, title)
	if kind := cartKind(stem); kind == ".p8" || kind == ".p8.png" {
		stem = stem[:len(stem)-len(kind)]
	}
	stem = strings.Trim(clean(stem, 100), ". ")
	if stem == "" {
		stem = "itch.io game"
	}
	return fmt.Sprintf("%s [itch %d]", stem, id)
}

// Link atomically where supported, then use rename on SD filesystems that
// lack hard links, with an existence check.
func install(dir, name string, data io.Reader, limit int64) error {
	dst := filepath.Join(dir, name)
	if _, err := os.Lstat(dst); !os.IsNotExist(err) {
		if err == nil {
			return errExists
		}
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	f, err := os.CreateTemp(dir, ".q2-pico8-itch-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	size, err := io.Copy(f, io.LimitReader(data, limit+1))
	if err == nil && size > limit {
		err = errors.New("File exceeds its size limit")
	}
	if err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	if err = os.Link(f.Name(), dst); err == nil {
		return nil
	} else if os.IsExist(err) {
		return errExists
	}
	if _, err = os.Lstat(dst); !os.IsNotExist(err) {
		return errExists
	}
	return os.Rename(f.Name(), dst)
}

func coverImage(c *http.Client, address string) (image.Image, error) {
	if address == "" {
		return nil, errVisit
	}
	raw, err := pageLimit(c, address, nil, "", 3<<20)
	if err != nil {
		return nil, err
	}
	cfg, _, err := image.DecodeConfig(bytes.NewReader(raw))
	if err != nil || cfg.Width < 1 || cfg.Height < 1 || cfg.Width > 1024 || cfg.Height > 1024 {
		return nil, errVisit
	}
	img, _, err := image.Decode(bytes.NewReader(raw))
	return img, err
}

func saveCover(img image.Image, dir, name string) bool {
	if img == nil {
		return false
	}
	var file bytes.Buffer
	if png.Encode(&file, img) != nil {
		return false
	}
	err := install(dir, name+".png", &file, 8<<20)
	return err == nil || errors.Is(err, errExists)
}

func installed(cartDir, stem string) bool {
	for _, ext := range []string{".p8.png", ".p8"} {
		if st, err := os.Stat(filepath.Join(cartDir, stem+ext)); err == nil && st.Mode().IsRegular() {
			return true
		}
	}
	return false
}

func download(c *http.Client, base, cartDir, coverDir string, output io.Writer) error {
	data, err := metadata(c, base)
	if err != nil {
		return err
	}
	if !isFree(data.Price) {
		return errVisit
	}
	stem := titleStem(data.Title, data.ID)
	if installed(cartDir, stem) {
		return errExists
	}
	if err := os.MkdirAll(cartDir, 0o755); err != nil {
		return errors.New("Could not write to the SD card")
	}
	art, _ := coverImage(c, data.Cover)
	cart, ext, err := downloadedCart(c, base, cartDir)
	if err != nil {
		web, webErr := webCart(c, base)
		if webErr != nil {
			if errors.Is(err, errConnect) {
				return err
			}
			return errNoCart
		}
		cart, ext = encodeCartPNG(web, art), ".p8.png"
	}
	name := stem + ext
	if err := install(cartDir, name, bytes.NewReader(cart), maxCart); err != nil {
		if errors.Is(err, errExists) {
			return err
		}
		return errors.New("Could not save the cart to the SD card")
	}
	if saveCover(art, coverDir, name) {
		fmt.Fprintln(output, "Cart and cover added to your library")
	} else {
		fmt.Fprintln(output, "Cart added to your library")
	}
	return nil
}

// downloadedCart tries the project's free downloadable files.
func downloadedCart(c *http.Client, base, stageDir string) ([]byte, string, error) {
	files, signed, csrf, key, err := guestUploads(c, base)
	if err != nil {
		return nil, "", err
	}
	rank := map[string]int{".p8.png": 0, ".p8": 1, ".zip": 2}
	sort.SliceStable(files, func(i, j int) bool {
		ri, iok := rank[cartKind(files[i].name)]
		rj, jok := rank[cartKind(files[j].name)]
		if iok != jok {
			return iok
		}
		return ri < rj
	})
	attempts := 0
	for _, file := range files {
		kind := cartKind(file.name)
		if file.id == "" || kind == "" {
			continue
		}
		if attempts++; attempts > 6 {
			break
		}
		cdn, err := postURL(c, base+"/file/"+file.id, signed,
			url.Values{"csrf_token": {csrf}, "download_key_id": {key}})
		if errors.Is(err, errVisit) {
			continue
		}
		if err != nil {
			return nil, "", err
		}
		// Stage on the SD card: /tmp is RAM on the Q2.
		staged, size, err := stageDownload(c, cdn, stageDir)
		if err != nil {
			return nil, "", err
		}
		cart, ext, err := cartFrom(staged, size, kind)
		staged.Close()
		if err == nil {
			return cart, ext, nil
		}
	}
	return nil, "", errNoCart
}

func run(args []string, output io.Writer) error {
	c := newClient()
	if len(args) == 5 && args[0] == "list" {
		n, _ := strconv.Atoi(args[3])
		return list(c, args[1], args[2], n, args[4], output)
	}
	if len(args) < 2 {
		return errVisit
	}
	u, ok := project(args[1])
	if !ok {
		return errVisit
	}
	base := strings.TrimRight(u.String(), "/")
	switch args[0] {
	case "download":
		if len(args) == 4 {
			return download(c, base, args[2], args[3], output)
		}
	case "qr":
		qr, err := qrcode.New(base, qrcode.Medium)
		if err != nil {
			return errVisit
		}
		for _, row := range qr.Bitmap() {
			for _, bit := range row {
				if bit {
					fmt.Fprint(output, "1")
				} else {
					fmt.Fprint(output, "0")
				}
			}
			fmt.Fprintln(output)
		}
		return nil
	}
	return errVisit
}

func main() {
	if os.Getenv("SSL_CERT_FILE") == "" {
		if exe, err := os.Executable(); err == nil {
			certs := filepath.Join(filepath.Dir(exe), "itch-ca.pem")
			if _, err := os.Stat(certs); err == nil {
				_ = os.Setenv("SSL_CERT_FILE", certs)
			}
		}
	}
	if err := run(os.Args[1:], os.Stdout); err != nil {
		fmt.Fprintln(os.Stdout, err)
		os.Exit(1)
	}
}
