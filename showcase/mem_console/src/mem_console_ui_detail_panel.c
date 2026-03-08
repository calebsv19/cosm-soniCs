#include "mem_console_ui_detail_panel.h"

#include <stdio.h>
#include <string.h>

typedef struct MemConsoleConnectionSummary {
    char kind[32];
    char neighbors[256];
    int count;
    int out_count;
    int in_count;
} MemConsoleConnectionSummary;

static const char *connection_kind_label(const char *kind) {
    if (!kind || !kind[0]) return "RELATED";
    if (strcmp(kind, "depends_on") == 0) return "DEPENDS";
    if (strcmp(kind, "supports") == 0) return "SUPPORTS";
    if (strcmp(kind, "references") == 0) return "REFS";
    if (strcmp(kind, "summarizes") == 0) return "SUMMARY";
    if (strcmp(kind, "related") == 0) return "RELATED";
    if (strcmp(kind, "implements") == 0) return "IMPLEMENTS";
    if (strcmp(kind, "blocks") == 0) return "BLOCKS";
    if (strcmp(kind, "contradicts") == 0) return "CONTRADICTS";
    return kind;
}

static int append_connection_neighbor(char *buffer, size_t cap, const char *title) {
    size_t used;
    int written;

    if (!buffer || cap == 0u || !title || !title[0]) {
        return 0;
    }
    if (strstr(buffer, title) != 0) {
        return 1;
    }

    used = strlen(buffer);
    if (used > 0u) {
        written = snprintf(buffer + used, cap - used, ", %s", title);
    } else {
        written = snprintf(buffer + used, cap - used, "%s", title);
    }
    return written > 0 && (size_t)written < (cap - used);
}

void mem_console_ui_detail_build_connection_summary(const MemConsoleState *state,
                                                    char *out_text,
                                                    size_t out_cap) {
    MemConsoleConnectionSummary summaries[8];
    int summary_count = 0;
    int i;
    size_t used = 0u;

    if (!out_text || out_cap == 0u) {
        return;
    }
    out_text[0] = '\0';
    if (!state || state->selected_item_id == 0 || state->graph_edge_count <= 0) {
        (void)snprintf(out_text, out_cap, "CONNECTIONS\nNONE");
        return;
    }

    memset(summaries, 0, sizeof(summaries));

    for (i = 0; i < state->graph_edge_count; ++i) {
        const MemConsoleGraphEdge *edge = &state->graph_edges[i];
        const char *kind = edge->kind[0] ? edge->kind : "related";
        int from_index = edge->from_index;
        int to_index = edge->to_index;
        int is_out = 0;
        int is_in = 0;
        int neighbor_index = -1;
        const char *neighbor_title = "UNKNOWN";
        int slot = -1;
        int s;

        if (from_index < 0 || to_index < 0 ||
            from_index >= state->graph_node_count ||
            to_index >= state->graph_node_count) {
            continue;
        }

        is_out = state->graph_nodes[from_index].item_id == state->selected_item_id;
        is_in = state->graph_nodes[to_index].item_id == state->selected_item_id;
        if (!is_out && !is_in) {
            continue;
        }

        neighbor_index = is_out ? to_index : from_index;
        if (neighbor_index >= 0 &&
            neighbor_index < state->graph_node_count &&
            state->graph_nodes[neighbor_index].title[0] != '\0') {
            neighbor_title = state->graph_nodes[neighbor_index].title;
        }

        for (s = 0; s < summary_count; ++s) {
            if (strcmp(summaries[s].kind, kind) == 0) {
                slot = s;
                break;
            }
        }
        if (slot < 0) {
            if (summary_count >= (int)(sizeof(summaries) / sizeof(summaries[0]))) {
                continue;
            }
            slot = summary_count++;
            (void)snprintf(summaries[slot].kind, sizeof(summaries[slot].kind), "%s", kind);
        }

        summaries[slot].count += 1;
        if (is_out) summaries[slot].out_count += 1;
        if (is_in) summaries[slot].in_count += 1;
        (void)append_connection_neighbor(summaries[slot].neighbors,
                                         sizeof(summaries[slot].neighbors),
                                         neighbor_title);
    }

    if (summary_count == 0) {
        (void)snprintf(out_text, out_cap, "CONNECTIONS\nNONE");
        return;
    }

    (void)snprintf(out_text, out_cap, "CONNECTIONS");
    used = strlen(out_text);
    for (i = 0; i < summary_count; ++i) {
        int written = snprintf(out_text + used,
                               out_cap - used,
                               "\n%s: %s",
                               connection_kind_label(summaries[i].kind),
                               summaries[i].neighbors[0] ? summaries[i].neighbors : "-");
        if (written <= 0 || (size_t)written >= (out_cap - used)) {
            break;
        }
        used += (size_t)written;
    }
}
