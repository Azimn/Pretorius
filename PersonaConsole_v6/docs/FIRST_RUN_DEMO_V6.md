# First Run Demo V6

Goal: launch, choose cartridge, talk, close, reopen, and see continuity without internet, GPU, account, model download, cloud service, Node at runtime, or SLM.

Build once with Cygwin: `make host cartridges`.

Run Pretorius: `./build/persona_host profiles/pretorius/pretorius.cart --stdio`.

Run demo characters by changing the cart path: profiles/friendly/friendly.cart, profiles/rival/rival.cart, profiles/quiet/quiet.cart, profiles/mentor/mentor.cart.

In stdio mode send JSON lines such as `{ "method":"chat", "text":"Good morning." }`, then `{ "method":"close" }`. Reopen with the same cart and ask what the character remembers.

For nontechnical packaging, wrap those commands in one-click PowerShell scripts and present a cartridge picker.
