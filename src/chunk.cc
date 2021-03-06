/********************************************************************
 * livegrep -- chunk.cc
 * Copyright (c) 2011-2013 Nelson Elhage
 *
 * This program is free software. You may use, redistribute, and/or
 * modify it under the terms listed in the COPYING file.
 ********************************************************************/
#include "chunk.h"
#include "radix_sort.h"
#include "radix_sorter.h"

#include <re2/re2.h>
#include <gflags/gflags.h>

#include <limits>

using re2::StringPiece;

DECLARE_bool(index);

void chunk::add_chunk_file(indexed_file *sf, const StringPiece& line)
{
    int l = (unsigned char*)line.data() - data;
    int r = l + line.size();
    chunk_file *f = NULL;
    int min_dist = numeric_limits<int>::max(), dist;
    for (vector<chunk_file>::iterator it = cur_file.begin();
         it != cur_file.end(); it ++) {
        if (l <= it->left)
            dist = max(0, it->left - r);
        else if (r >= it->right)
            dist = max(0, l - it->right);
        else
            dist = 0;
        assert(dist == 0 || r < it->left || l > it->right);
        if (dist < min_dist) {
            min_dist = dist;
            f = &(*it);
        }
    }
    if (f && min_dist < kMaxGap) {
        f->expand(l, r);
        return;
    }
    chunk_files++;
    cur_file.push_back(chunk_file());
    chunk_file& cf = cur_file.back();
    cf.files.push_front(sf);
    cf.left = l;
    cf.right = r;
}

void chunk::finish_file() {
    int right = -1;
    sort(cur_file.begin(), cur_file.end());
    for (vector<chunk_file>::iterator it = cur_file.begin();
         it != cur_file.end(); it ++) {
        assert(right < it->left);
        right = max(right, it->right);
    }
    files.insert(files.end(), cur_file.begin(), cur_file.end());
    cur_file.clear();
}

int chunk::chunk_files = 0;

void radix_sorter::sort(uint32_t *l, uint32_t *r) {
    cmp_suffix cmp(*this);
    indexer idx(*this);
    msd_radix_sort(l, r, idx, cmp);

#ifdef DEBUG_RADIX_SORT
    assert(is_sorted(l, r, cmp));
#endif
}

void chunk::finalize() {
    if (FLAGS_index) {
        for (int i = 0; i < size; i++)
            suffixes[i] = i;
        radix_sorter sorter(data, size);
        sorter.sort(suffixes, suffixes + size);
    }
}

void chunk::finalize_files() {
    sort(files.begin(), files.end());

    vector<chunk_file>::iterator out, in;
    out = in = files.begin();
    while (in != files.end()) {
        *out = *in;
        ++in;
        while (in != files.end() &&
               out->left == in->left &&
               out->right == in->right) {
            out->files.push_back(in->files.front());
            ++in;
        }
        ++out;
    }
    files.resize(out - files.begin());
    build_tree();
}

void chunk::build_tree() {
    assert(is_sorted(files.begin(), files.end()));
    cf_root = build_tree(0, files.size());
}

chunk_file_node *chunk::build_tree(int left, int right) {
    if (right == left)
        return 0;
    int mid = (left + right) / 2;
    chunk_file_node *node = new chunk_file_node;

    node->chunk = &files[mid];
    node->left  = build_tree(left, mid);
    node->right = build_tree(mid + 1, right);
    node->right_limit = node->chunk->right;
    if (node->left && node->left->right_limit > node->right_limit)
        node->right_limit = node->left->right_limit;
    if (node->right && node->right->right_limit > node->right_limit)
        node->right_limit = node->right->right_limit;
    assert(!node->left  || *(node->left->chunk) < *(node->chunk));
    assert(!node->right || *(node->chunk) < *(node->right->chunk));
    return node;
}
