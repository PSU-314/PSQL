#ifndef BTREE_H
#define BTREE_H

#include <cstdint>
#include "table.h"
#include "cursor.h"

Cursor tableFind(Table* table, uint32_t key);

void insert_leaf_node(Cursor* cursor, uint32_t key, const void* rowData);

#endif