/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-layer pre-validation of a cartridge's Lua syntax.
 *
 * Why this exists: FAKE-08's Vm::LoadCart only parses the .p8 file
 * sections — it does not compile the Lua. The syntax error is only
 * found when the VM first ticks the cart (inside GameLoop), at which
 * point upstream queues a switch to its built-in BIOS menu and the
 * load error is clobbered when the BIOS cart is loaded, so the user
 * sees the plain BIOS menu with no error for their cart.
 *
 * q2_cart_lua_ok() runs the same check the VM would run, before the
 * VM ever starts: it parses the cart with upstream's own Cart class
 * (so .p8 and .p8.png, #include handling, etc. all match) and then
 * compiles the resulting Lua with z8lua's own compiler. A syntax
 * error is reported with the compiler's message, and the caller can
 * show it on screen instead of starting the VM. This changes no
 * upstream FAKE-08 behaviour — it only decides, in the Q2 host,
 * whether to start the VM at all.
 */
#ifndef Q2_CART_VALIDATE_H
#define Q2_CART_VALIDATE_H
#include <string>
/* Returns true when the cart parses and its Lua compiles.
 * On false, error_out holds the load/syntax error (never empty).
 * filename is the cart name; cart_dir is the directory it lives in. */
bool q2_cart_lua_ok(const std::string &filename,
                    const std::string &cart_dir,
                    std::string &error_out);
#endif /* Q2_CART_VALIDATE_H */
