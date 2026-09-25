#include "notemdx/compiler.hpp"
#include <iostream>

int main() {
    // The host can obtain this CP932 text from a file, editor, or network.
    const notemdx::Source source{"memory.mml", "#title \"Module example\"\nP F4 @0 n0,4\n"};
    notemdx::Options options;
    const auto result = notemdx::compile(source, options);
    if (!result.ok()) {
        for (const auto& d : result.diagnostics)
            std::cerr << d.file << ':' << d.line << ':' << d.column << ' ' << d.message << '\n';
        return 1;
    }
    // result.mdx is an owned byte vector; pass it to a player or file writer.
    std::cout << result.mdx.size() << " MDX bytes\n";
    return 0;
}
