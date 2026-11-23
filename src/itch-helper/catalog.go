// SPDX-License-Identifier: GPL-2.0-or-later
// itch.io catalog for PICO-8 games, adapted from Pocket Boy's catalog.go.
package main

import (
	"archive/zip"
	"bytes"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"

	"golang.org/x/net/html"
)

// itch.io's own listing of games made with PICO-8, with its sort facets.
const catalogTool = "made-with-pico-8"

var sortPaths = map[string]string{"popular": "", "newest": "newest", "top-rated": "top-rated"}

type catalogGame struct {
	id                                    int64
	title, creator, price, genre, address string
}

func nodeAttr(n *html.Node, name string) string {
	for _, attr := range n.Attr {
		if attr.Key == name {
			return attr.Val
		}
	}
	return ""
}

func nodeClass(n *html.Node, class string) bool {
	for _, part := range strings.Fields(nodeAttr(n, "class")) {
		if part == class {
			return true
		}
	}
	return false
}

func nodeText(n *html.Node) string {
	var text strings.Builder
	var walk func(*html.Node)
	walk = func(node *html.Node) {
		if node.Data == "svg" {
			return
		}
		if node.Type == html.TextNode {
			text.WriteString(node.Data)
		}
		for child := node.FirstChild; child != nil; child = child.NextSibling {
			walk(child)
		}
	}
	walk(n)
	return strings.TrimSpace(text.String())
}

func catalogEntries(raw []byte) ([]catalogGame, bool, error) {
	root, err := html.Parse(bytes.NewReader(raw))
	if err != nil {
		return nil, false, err
	}
	var entries []catalogGame
	seen := map[int64]bool{}
	more, grid := false, false
	var walk func(*html.Node, *catalogGame)
	walk = func(n *html.Node, game *catalogGame) {
		var entry catalogGame
		cell := n.Type == html.ElementNode && nodeClass(n, "game_cell")
		if cell {
			entry.id, _ = strconv.ParseInt(nodeAttr(n, "data-game_id"), 10, 64)
			game = &entry
		}
		if n.Type == html.ElementNode {
			if nodeClass(n, "browse_game_grid") {
				grid = true
			}
			if n.Data == "link" && nodeAttr(n, "rel") == "next" {
				more = true
			}
			if game != nil {
				switch {
				case n.Data == "a" && nodeClass(n, "title") && nodeClass(n, "game_link"):
					game.address = nodeAttr(n, "href")
					game.title = nodeText(n)
				case nodeClass(n, "game_author"):
					game.creator = nodeText(n)
				case nodeClass(n, "price_value"):
					game.price = nodeText(n)
				case nodeClass(n, "game_genre"):
					game.genre = nodeText(n)
				}
			}
		}
		for child := n.FirstChild; child != nil; child = child.NextSibling {
			walk(child, game)
		}
		if cell && entry.id > 0 && !seen[entry.id] && len(entries) < 80 {
			u, ok := project(entry.address)
			if ok && len(entry.address) < 250 && entry.title != "" {
				if entry.creator == "" {
					entry.creator = strings.TrimSuffix(u.Hostname(), ".itch.io")
				}
				entry.address = strings.TrimRight(entry.address, "/")
				seen[entry.id] = true
				entries = append(entries, entry)
			}
		}
	}
	walk(root, nil)
	// Itch uses an empty results message in place of the grid for an empty filter.
	if !grid && len(entries) == 0 && !bytes.Contains(raw, []byte("browse_game_grid")) &&
		!bytes.Contains(raw, []byte("No games")) && !bytes.Contains(raw, []byte("no_results")) &&
		!bytes.Contains(raw, []byte("0 results")) {
		return nil, false, errors.New("Could not read the itch.io catalog")
	}
	return entries, more, nil
}

func searchWords(value string) []string {
	return strings.Fields(strings.NewReplacer("-", " ", "_", " ").Replace(strings.ToLower(value)))
}

func matchesQuery(game catalogGame, query string) bool {
	words := strings.Join(searchWords(game.title+" "+game.creator+" "+game.address), " ")
	for _, term := range searchWords(query) {
		if !strings.Contains(words, term) {
			return false
		}
	}
	return true
}

// ZIP-only projects need a directory check. The CDN must support a bounded
// suffix range. If the archive cannot be inspected, skip it in the catalog.
const zipTailSize = 128 << 10

type zipTail struct {
	data   []byte
	offset int64
}

func (tail zipTail) ReadAt(p []byte, offset int64) (int, error) {
	offset -= tail.offset
	if offset < 0 || offset >= int64(len(tail.data)) {
		return 0, io.EOF
	}
	n := copy(p, tail.data[int(offset):])
	if n != len(p) {
		return n, io.EOF
	}
	return n, nil
}

func archiveHasCart(c *http.Client, address string) bool {
	req, err := http.NewRequest(http.MethodGet, address, nil)
	if err != nil || !permitted(req.URL) {
		return false
	}
	req.Header.Set("User-Agent", agent)
	req.Header.Set("Accept-Encoding", "identity")
	req.Header.Set("Range", fmt.Sprintf("bytes=-%d", zipTailSize))
	res, err := c.Do(req)
	if err != nil {
		return false
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusPartialContent {
		return false
	}
	var start, end, size int64
	if n, _ := fmt.Sscanf(res.Header.Get("Content-Range"), "bytes %d-%d/%d", &start, &end, &size); n != 3 || start < 0 || size <= 0 || size > maxArchive || end != size-1 || end-start+1 > zipTailSize {
		return false
	}
	raw, err := limited(res.Body, zipTailSize)
	if err != nil || int64(len(raw)) != end-start+1 {
		return false
	}
	reader, err := zip.NewReader(zipTail{data: raw, offset: start}, size)
	if err != nil {
		return false
	}
	return len(archiveCandidates(reader.File)) > 0
}

// catalogKind reports how a project offers a PICO-8 cart: "cart" for a
// downloadable .p8/.p8.png (directly or in a ZIP), "web" for an embedded
// PICO-8 web export only (verified at download time), or "" for neither.
func catalogKind(c *http.Client, game catalogGame) (string, error) {
	raw, err := page(c, game.address, nil, "")
	if err != nil {
		return "", err
	}
	hasZip := false
	for _, file := range uploads(raw) {
		switch cartKind(file.name) {
		case ".p8", ".p8.png":
			return "cart", nil
		case ".zip":
			hasZip = true
		}
	}
	web := ""
	if embedURL(raw) != "" {
		web = "web"
	}
	if !hasZip || !isFree(game.price) {
		return web, nil
	}
	// A separate cookie jar keeps guest download sessions for projects on the
	// same creator domain from interfering with one another.
	guest := *c
	guest.Jar = newClient().Jar
	files, signed, csrf, key, err := guestUploads(&guest, game.address)
	if err != nil {
		return web, nil
	}
	tried := 0
	for _, file := range files {
		kind := cartKind(file.name)
		if file.id == "" {
			continue
		}
		if kind == ".p8" || kind == ".p8.png" {
			return "cart", nil
		}
		if kind != ".zip" || tried >= 2 {
			continue
		}
		tried++
		cdn, err := postURL(&guest, game.address+"/file/"+file.id, signed,
			url.Values{"csrf_token": {csrf}, "download_key_id": {key}})
		if err == nil && archiveHasCart(&guest, cdn) {
			return "cart", nil
		}
	}
	return web, nil
}

func catalogURL(order string, number int) (string, bool) {
	sortPath, ok := sortPaths[order]
	if !ok || number < 1 || number > 100 {
		return "", false
	}
	parts := []string{"https://itch.io/games"}
	if sortPath != "" {
		parts = append(parts, sortPath)
	}
	parts = append(parts, catalogTool)
	return fmt.Sprintf("%s?page=%d", strings.Join(parts, "/"), number), true
}

func list(c *http.Client, price, order string, number int, query string, output io.Writer) error {
	address, ok := catalogURL(order, number)
	if !ok || (price != "all" && price != "free" && price != "paid") {
		return errVisit
	}
	raw, err := page(c, address, nil, "")
	if err != nil {
		return err
	}
	entries, more, err := catalogEntries(raw)
	if err != nil {
		return err
	}
	type result struct {
		kind string
		err  error
	}
	checked := make([]result, len(entries))
	var workers sync.WaitGroup
	jobs := make(chan int)
	for range 4 {
		workers.Add(1)
		go func() {
			defer workers.Done()
			for i := range jobs {
				checked[i].kind, checked[i].err = catalogKind(c, entries[i])
			}
		}()
	}
	attempted := 0
	for index, game := range entries {
		if (price == "all" || (price == "free") == isFree(game.price)) && matchesQuery(game, query) {
			jobs <- index
			attempted++
		}
	}
	close(jobs)
	workers.Wait()
	matched, failed := 0, 0
	for _, result := range checked {
		if result.kind != "" {
			matched++
		}
		if result.err != nil && !errors.Is(result.err, errVisit) {
			failed++
		}
	}
	if matched == 0 && failed > 0 && failed == attempted {
		return errors.New("Couldn't check this page. Try again")
	}
	if more && number < 100 {
		fmt.Fprintln(output, "P 1")
	} else {
		fmt.Fprintln(output, "P 0")
	}
	for i, game := range entries {
		if checked[i].kind != "" {
			fmt.Fprintf(output, "%d\t%s\t%s\t%s\t%s\t%s\n", game.id, clean(game.title, 95),
				clean(game.creator, 45), priceLabel(game.price), game.address, checked[i].kind)
		}
	}
	return nil
}
