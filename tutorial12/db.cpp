#include <iostream>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

// 识别元命令
enum MetaCommandResult
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
};

// insert select语句解析后的状态
enum PrepareResult
{
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
};

// 
enum StatementType
{
    STATEMENT_INSERT,
    STATEMENT_SELECT
};

enum ExecuteResult
{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_DUPLICATE_KEY
};

// 
enum NodeType
{
    NODE_INTERNAL,
    NODE_LEAF
};

// ****** 一行数据的定义 ******
// 主要定义数据大小、offset、序列化和反序列化 一行数据存储void地址 和 void地址返回一行数据
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
class Row
{
public:
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
    Row()
    {
        id = 0;
        username[0] = '\0';
        email[0] = '\0';
    }
    Row(uint32_t id, const char *username, const char *email)
    {
        this->id = id;
        strncpy(this->username, username, COLUMN_USERNAME_SIZE + 1);
        strncpy(this->email, email, COLUMN_EMAIL_SIZE + 1);
    }
};
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

void serialize_row(Row &source, void *destination)
{
    memcpy((char *)destination + ID_OFFSET, &(source.id), ID_SIZE);
    strncpy((char *)destination + USERNAME_OFFSET, source.username, USERNAME_SIZE);
    strncpy((char *)destination + EMAIL_OFFSET, source.email, EMAIL_SIZE);
}
void deserialize_row(void *source, Row &destination)
{
    memcpy(&(destination.id), (char *)source + ID_OFFSET, ID_SIZE);
    strncpy(destination.username, (char *)source + USERNAME_OFFSET, USERNAME_SIZE);
    strncpy(destination.email, (char *)source + EMAIL_OFFSET, EMAIL_SIZE);
}

// 一页 4K
#define TABLE_MAX_PAGES 100
const uint32_t PAGE_SIZE = 4096;


// 公共节点 
//    叶子节点 
//    内部节点
// 公共节点头：节点类型 + 是否根节点 + 指向其父节点的指针
// COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE


/* 
 * Common Node Header Layout
 */
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// 公共节点头部分的取值
class Node
{
protected:
    void *node;
public:
    Node() {}
    Node(void *node) : node(node) {}
    // 注意取值都需要转换成对应指针类型
    NodeType get_node_type()
    {
        uint8_t value = *((uint8_t *)((char *)node + NODE_TYPE_OFFSET));
        return (NodeType)value;
    }
    
    void set_node_type(NodeType type)
    {
        *((uint8_t *)((char *)node + NODE_TYPE_OFFSET)) = (uint8_t)type;
    }
    
    void *get_node()
    {
        return node;
    }
    
    bool is_node_root()
    {
        uint8_t value = *((uint8_t *)((char *)node + IS_ROOT_OFFSET));
        return value == 1;
    }
    void set_node_root(bool is_root)
    {
        *((uint8_t *)((char *)node + IS_ROOT_OFFSET)) = is_root ? 1 : 0;
    }
    
    uint32_t *node_parent()
    {
        return (uint32_t *)((char *)node + PARENT_POINTER_OFFSET);
    }
    
    virtual uint32_t get_node_max_key();
};

/*
 * Leaf Node Header Layout
 */
// 叶子节点头：公共节点头 + 胞元数量 + 下一个叶节点 
// LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;

/*
 * Leaf Node Body Layout
 */
// 叶节点体：胞元 = 关键词（id） + 值 （数据行）
//      LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;

// 页大小 - 叶节点头大小
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

// 叶节点分裂
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;


class LeafNode : public Node
{
public:
    LeafNode() {}
    LeafNode(void *node) : Node(node) {}

    void initialize_leaf_node()
    {
        // 公共头
        set_node_type(NODE_LEAF);
        set_node_root(false);
        // 叶子头
        *leaf_node_num_cells() = 0;
        *leaf_node_next_leaf() = 0; // 0 represents no sibling
    }
    
    // 叶节点头
    uint32_t *leaf_node_num_cells()
    {
        return (uint32_t *)((char *)node + LEAF_NODE_NUM_CELLS_OFFSET);
    }
    u_int32_t *leaf_node_next_leaf()
    {
        return (u_int32_t *)((char *)node + LEAF_NODE_NEXT_LEAF_OFFSET);
    }
    
    // 叶节点体
    void *leaf_node_cell(uint32_t cell_num)
    {
        return (char *)node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
    }
    uint32_t *leaf_node_key(uint32_t cell_num)
    {
        // uint32_t 因关键词确定
        return (uint32_t *)leaf_node_cell(cell_num);
    }
    void *leaf_node_value(uint32_t cell_num)
    {
        return (char *)leaf_node_cell(cell_num) + LEAF_NODE_KEY_SIZE;
    }
    
    // 得到最右边的键值
    uint32_t get_node_max_key() override
    {
        return *leaf_node_key(*leaf_node_num_cells() - 1);
    }

};

/*
 * Internal Node Header Layout
 */
// 内部节点头： 公共节点头 + 关键词数量 + 右孩子 
//      INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE;
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;
// 
/*
 * Internal Node Body Layout
 */
// 内部节点体： 胞元 =  孩子指针　+　关键词
//      INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE = INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
/* Keep this small for testing */
const uint32_t INTERNAL_NODE_MAX_CELLS = 3;

class InternalNode : public Node
{
public:
    InternalNode() {}
    InternalNode(void *node) : Node(node) {}

    void initialize_internal_node()
    {
        set_node_type(NODE_INTERNAL);
        set_node_root(false);
        
        *internal_node_num_keys() = 0;
    }
    // 内部节点头
    uint32_t *internal_node_num_keys()
    {
        return (uint32_t *)((char *)node + INTERNAL_NODE_NUM_KEYS_OFFSET);
    }
    u_int32_t *internal_node_right_child()
    {
        return (u_int32_t *)((char *)node + INTERNAL_NODE_RIGHT_CHILD_OFFSET);
    }
    // 内部节点体
    uint32_t *internal_node_cell(uint32_t cell_num)
    {
        return (uint32_t *)((char *)node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE);
    }
    // 内节点右孩子和其他孩子分开存储
    uint32_t *internal_node_child(uint32_t child_num)
    {
        uint32_t num_keys = *internal_node_num_keys();
        if (child_num > num_keys)
        {
            std::cout << "Tried to access child_num " << child_num << " > num_keys " << num_keys << std::endl;
            exit(EXIT_FAILURE);
        }
        else if (child_num == num_keys)
        {
            return internal_node_right_child();
        }
        else
        {
            return internal_node_cell(child_num);
        }
    }
    
    
    uint32_t *internal_node_key(uint32_t key_num)
    {
        //　关键词在后面　internal_node_cell返回类型为　uint32_t*
        return internal_node_cell(key_num) + INTERNAL_NODE_CHILD_SIZE / sizeof(uint32_t);
    }
 
    uint32_t get_node_max_key() override
    {
        return *internal_node_key(*internal_node_num_keys() - 1);
    }
    
    /*
        Return the index of the child which should contain
        the given key.
        就是该插入的位置
    */
    uint32_t internal_node_find_child(uint32_t key)
    {

        uint32_t num_keys = *internal_node_num_keys();

        /* Binary search */
        uint32_t min_index = 0;
        uint32_t max_index = num_keys; /* there is one more child than key */

        while (min_index != max_index)
        {
            uint32_t index = (min_index + max_index) / 2;
            uint32_t key_to_right = *internal_node_key(index);
            if (key_to_right >= key)
            {
                max_index = index;
            }
            else
            {
                min_index = index + 1;
            }
        }

        return min_index;
    }
    // 二分查找找到键值序号 
    void update_internal_node_key(uint32_t old_key, uint32_t new_key)
    {
        uint32_t old_child_index = internal_node_find_child(old_key);
        *internal_node_key(old_child_index) = new_key;
    }
};

// 各自虚函数写好由父类调用
uint32_t Node::get_node_max_key()
{
    if (get_node_type() == NODE_LEAF)
    {
        LeafNode *leaf_node = (LeafNode *)this;
        return *leaf_node->leaf_node_key(*leaf_node->leaf_node_num_cells() - 1);
    }
    else
    {
        InternalNode *internal_node = (InternalNode *)this;
        return *internal_node->internal_node_key(*internal_node->internal_node_num_keys() - 1);
    }
}


// *************** 实际页 
class Pager
{
private:
    // 文件描述符和pages存在对应的映射关系
    // 保存在硬盘
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    // 保存在内存
    void *pages[TABLE_MAX_PAGES];
public:
    Pager(const char *filename);
    // 从内存 / 磁盘 读入 写入
    void *get_page(uint32_t page_num);
    void pager_flush(uint32_t page_num);
    
    void print_tree(uint32_t page_num, uint32_t indentation_level);
    uint32_t get_unused_page_num();
    friend class Table;
};

Pager::Pager(const char *filename)
{
    file_descriptor = open(filename,
                           O_RDWR |     // Read/Write mode
                               O_CREAT, // Create file if it does not exist
                           S_IWUSR |    // User write permission
                               S_IRUSR  // User read permission
    );
    if (file_descriptor < 0)
    {
        std::cerr << "Error: cannot open file " << filename << std::endl;
        exit(EXIT_FAILURE);
    }

    file_length = lseek(file_descriptor, 0, SEEK_END);
    num_pages = file_length / PAGE_SIZE;
    if (file_length % PAGE_SIZE != 0)
    {
        std::cerr << "Db file is not a whole number of pages. Corrupt file." << std::endl;
        exit(EXIT_FAILURE);
    }
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        pages[i] = nullptr;
    }
}

void *Pager::get_page(uint32_t page_num)
{
    if (page_num > TABLE_MAX_PAGES)
    {
        std::cout << "Tried to fetch page number out of bounds. " << page_num << " > "
                  << TABLE_MAX_PAGES << std::endl;
        exit(EXIT_FAILURE);
    }
    if (pages[page_num] == nullptr)
    {
        // Cache miss. Allocate memory and load from file.
        void *page = malloc(PAGE_SIZE);
        // num_pages是磁盘的页总数
        uint32_t num_pages = file_length / PAGE_SIZE;
        // We might save a partial page at the end of the file
        if (file_length % PAGE_SIZE)
        {
            num_pages += 1;
        }
        if (page_num <= num_pages)
        {
            lseek(file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1)
            {
                std::cout << "Error reading file: " << errno << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        pages[page_num] = page;
        if (page_num >= num_pages)
        {
            this->num_pages = page_num + 1;
        }
    }
    return pages[page_num];
}

void Pager::pager_flush(uint32_t page_num)
{
    if (pages[page_num] == nullptr)
    {
        std::cout << "Tried to flush null page" << std::endl;
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1)
    {
        std::cout << "Error seeking: " << errno << std::endl;
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = write(file_descriptor, pages[page_num], PAGE_SIZE);
    if (bytes_written == -1)
    {
        std::cout << "Error writing: " << errno << std::endl;
        exit(EXIT_FAILURE);
    }
}

void indent(uint32_t level)
{
    for (uint32_t i = 0; i < level; i++)
    {
        std::cout << "  ";
    }
}
void Pager::print_tree(uint32_t page_num, uint32_t indentation_level)
{
    Node *node = new Node(get_page(page_num));
    uint32_t num_keys, child;
    switch (node->get_node_type())
    {
    case (NODE_LEAF):
        num_keys = *((LeafNode *)node)->leaf_node_num_cells();
        indent(indentation_level);
        std::cout << "- leaf (size " << num_keys << ")" << std::endl;
        for (uint32_t i = 0; i < num_keys; i++)
        {
            indent(indentation_level + 1);
            std::cout << "- " << *((LeafNode *)node)->leaf_node_key(i) << std::endl;
        }
        break;
    case (NODE_INTERNAL):
        num_keys = *((InternalNode *)node)->internal_node_num_keys();
        indent(indentation_level);
        std::cout << "- internal (size " << num_keys << ")" << std::endl;
        for (uint32_t i = 0; i < num_keys; i++)
        {
            child = *((InternalNode *)node)->internal_node_child(i);
            print_tree(child, indentation_level + 1);
            indent(indentation_level + 1);
            std::cout << "- key " << *((InternalNode *)node)->internal_node_key(i) << std::endl;
        }
        child = *((InternalNode *)node)->internal_node_right_child();
        print_tree(child, indentation_level + 1);
        break;
    }
    delete node;
}

/*
Until we start recycling free pages, new pages will always
go onto the end of the database file
*/
uint32_t Pager::get_unused_page_num()
{
    return num_pages;
}

//**************** 操作table的游标 用于实际的插入

class Table;
class Cursor
{
private:
    Table *table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
public:
    Cursor(Table *table);
    Cursor(Table *table, uint32_t page_num, uint32_t cell_num);
    
    void *cursor_value();
    void cursor_advance();
    
    void leaf_node_insert(uint32_t key, Row &value);
    void leaf_node_split_and_insert(uint32_t key, Row &value);
    void internal_node_insert(uint32_t key, uint32_t right_child);
    ~Cursor();
    friend class DB;
};

class Table
{
private:
    uint32_t root_page_num;
    Pager pager;

public:
    Table(const char *filename) : pager(filename)
    {
        root_page_num = 0;
        if (pager.num_pages == 0)
        {
            // New file. Initialize page 0 as leaf node.
            LeafNode root_node = pager.get_page(0);
            root_node.initialize_leaf_node();
            root_node.set_node_root(true);// 叶子节点头有些没初始化
        }
    }
    
    Cursor *table_find(uint32_t key);
    Cursor *internal_node_find(uint32_t key, uint32_t page_num);
    void create_new_root(uint32_t right_child_page_num);
    
    ~Table();

    friend class Cursor;
    friend class DB;
};


// 递归搜索直到叶节点 
// 根节点为叶节点直接返回
Cursor *Table::table_find(uint32_t key)
{
    Node root_node = pager.get_page(root_page_num);
    if (root_node.get_node_type() == NODE_LEAF)
    {
        return new Cursor(this, root_page_num, key);
    }
    else
    {
        return internal_node_find(root_page_num, key);
    }
}

// 递归搜索 
Cursor *Table::internal_node_find(uint32_t page_num, uint32_t key)
{
    // 1.page_num -> pager调用get_page -> 得到节点
    // 2.key -> InternalNode调用internal_node_find_child -> 得到孩子索引位置
    // 3.child_index -> internal_node_child -> 得到孩子实际的页码
    InternalNode node = pager.get_page(page_num);
    uint32_t child_index = node.internal_node_find_child(key);
    uint32_t child_num = *node.internal_node_child(child_index);
    Node child = pager.get_page(child_num);
    switch (child.get_node_type())
    {
    case NODE_INTERNAL:
        return internal_node_find(child_num, key);
    case NODE_LEAF:
    default:
        return new Cursor(this, child_num, key);
    }
}

///************
// 查询语句光标
Cursor::Cursor(Table *table)
{
    // id = 0 时的光标
    Cursor *cursor = table->table_find(0);
    
    this->table = table;
    this->page_num = cursor->page_num;
    this->cell_num = cursor->cell_num;
    
    LeafNode root_node = table->pager.get_page(cursor->page_num);
    uint32_t num_cells = *root_node.leaf_node_num_cells();
    this->end_of_table = (num_cells == 0);
}

// insert语句光标
// 由key的二分查找得到cursor指向位置
Cursor::Cursor(Table *table, uint32_t page_num, uint32_t key)
{
    this->table = table;
    this->page_num = page_num;
    this->end_of_table = false;

    LeafNode root_node = table->pager.get_page(page_num);
    uint32_t num_cells = *root_node.leaf_node_num_cells();
    // Binary search
    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;
    while (one_past_max_index != min_index)
    {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *root_node.leaf_node_key(index);
        if (key == key_at_index)
        {
            this->cell_num = index;
            return;
        }
        if (key < key_at_index)
        {
            one_past_max_index = index;
        }
        else
        {
            min_index = index + 1;
        }
    }
    this->cell_num = min_index;
}

//  将key + row 插入
// 未满情况下插入光标指向位置
void Cursor::leaf_node_insert(uint32_t key, Row &value)
{
    // cursor当前指向页是否满
    LeafNode leaf_node = table->pager.get_page(page_num);
    uint32_t num_cells = *leaf_node.leaf_node_num_cells();
    if (num_cells >= LEAF_NODE_MAX_CELLS)
    {
        // Node full
        leaf_node_split_and_insert(key, value);
        return;
    }
    // 插入到cursor指向的cell_num位置
    if (cell_num < num_cells)
    {
        // make room for new cell
        for (uint32_t i = num_cells; i > cell_num; i--)
        {
            memcpy(leaf_node.leaf_node_cell(i), leaf_node.leaf_node_cell(i - 1),LEAF_NODE_CELL_SIZE);
        }
    }
    // insert new cell
    *leaf_node.leaf_node_num_cells() += 1;
    *leaf_node.leaf_node_key(cell_num) = key;
    serialize_row(value, leaf_node.leaf_node_value(cell_num));
}


// 由 table page_num cell_num 得到指针
void *Cursor::cursor_value()
{
    void *page = table->pager.get_page(page_num);
    return LeafNode(page).leaf_node_value(cell_num);
}

// 通过判断是否到叶子节点的结尾，转向leaf_node_next_leaf
void Cursor::cursor_advance()
{
    LeafNode leaf_node = table->pager.get_page(page_num);
    cell_num += 1;
    if (cell_num >= *leaf_node.leaf_node_num_cells())
    {
        /* Advance to next leaf node */
        uint32_t next_page_num = *leaf_node.leaf_node_next_leaf();
        if (next_page_num == 0)
        {
            /* This was rightmost leaf */
            end_of_table = true;
        }
        else
        {
            page_num = next_page_num;
            cell_num = 0;
        }
    }
}

// 将孩子（叶节点or内部节点） 插入
// 仅考虑边界情况即右孩子的替换
void Cursor::internal_node_insert(uint32_t parent_page_num, uint32_t child_page_num)
{
    /*
     Add a new child/key pair to parent that corresponds to child
    */
    InternalNode parent = table->pager.get_page(parent_page_num);
    Node child = table->pager.get_page(child_page_num);
    // 孩子（不知道叶节点还是内部节点）的最大key 在父（内部节点中）找到相应位置插入 
    uint32_t child_max_key = child.get_node_max_key();
    uint32_t index = parent.internal_node_find_child(child_max_key);
    // 扩大一个key
    uint32_t original_num_keys = *parent.internal_node_num_keys();
    *parent.internal_node_num_keys() = original_num_keys + 1;

    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS)
    {
        std::cout << "Need to implement splitting internal node" << std::endl;
        exit(EXIT_FAILURE);
    }

    uint32_t right_child_page_num = *parent.internal_node_right_child();
    Node right_child = table->pager.get_page(right_child_page_num);
    if (child_max_key > right_child.get_node_max_key())
    {
        /* Replace right child */
        // 最新的 key child 存之前的 右孩子
        *parent.internal_node_child(original_num_keys) = right_child_page_num;
        *parent.internal_node_key(original_num_keys) = right_child.get_node_max_key();
        *parent.internal_node_right_child() = child_page_num;
    }
    else
    {
        /* Make room for the new cell */
        // original_num_keys为空位置 后移
        for (uint32_t i = original_num_keys; i > index; i--)
        {
            void *destination = parent.internal_node_cell(i);
            void *source = parent.internal_node_cell(i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        
        *parent.internal_node_child(index) = child_page_num;
        *parent.internal_node_key(index) = child_max_key;
    }
}


// 叶节点分裂
void Cursor::leaf_node_split_and_insert(uint32_t key, Row &value)
{
    /*
    Create a new node and move half the cells over.
    Insert the new value in one of the two nodes.
    Update parent or create a new parent.
    */
    LeafNode old_node = table->pager.get_page(page_num);
    uint32_t old_max = old_node.get_node_max_key();

    uint32_t new_page_num = table->pager.get_unused_page_num();
    LeafNode new_node = table->pager.get_page(new_page_num);
    new_node.initialize_leaf_node();// 初始化叶子节点
    
    // 共同的父节点
    *new_node.node_parent() = *old_node.node_parent();
    // old -> new -> old的next
    *new_node.leaf_node_next_leaf() = *old_node.leaf_node_next_leaf();
    *old_node.leaf_node_next_leaf() = new_page_num;

    /*
    All existing keys plus new key should be divided
    evenly between old (left) and new (right) nodes.
    Starting from the right, move each key to correct position.
    */
    for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--)
    {
        LeafNode destination_node;
        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT)
        {
            destination_node = new_node;
        }
        else
        {
            destination_node = old_node;
        }
        
        // 得到对应叶节点体
        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        LeafNode destination = destination_node.leaf_node_cell(index_within_node);
        // 注意还要在对应位置插入最新的元素
        // 因为前后会有i的不同
        if (i == cell_num)
        {
            serialize_row(value,destination_node.leaf_node_value(index_within_node));
            *destination_node.leaf_node_key(index_within_node) = key;
        }
        else if (i > cell_num)
        {
            
            memcpy(destination.get_node(), old_node.leaf_node_cell(i - 1), LEAF_NODE_CELL_SIZE);
        }
        else
        {
            memcpy(destination.get_node(), old_node.leaf_node_cell(i), LEAF_NODE_CELL_SIZE);
        }
    }
    /* Update cell count on both leaf nodes */
    // old的前半部分
    // new的前半部分
    *old_node.leaf_node_num_cells() = LEAF_NODE_LEFT_SPLIT_COUNT;
    *new_node.leaf_node_num_cells() = LEAF_NODE_RIGHT_SPLIT_COUNT;
    if (old_node.is_node_root())
    {
        return table->create_new_root(new_page_num);
    }
    else
    {
        // 核心逻辑： key -> index -> 更新key
        // 内部节点存的 页码 和 最大key
        // 父节点中的更新
        uint32_t parent_page_num = *old_node.node_parent();
        uint32_t new_max = old_node.get_node_max_key();
        InternalNode parent = table->pager.get_page(parent_page_num);
        // old node更新键值
        parent.update_internal_node_key(old_max, new_max);
        // 
        internal_node_insert(parent_page_num, new_page_num);
 
        return;
    }
}

Cursor::~Cursor()
{
    //delete table;
}


// 右孩子交给这边
void Table::create_new_root(uint32_t right_child_page_num)
{
    /*
    Handle splitting the root.
    Old root copied to new page, becomes left child.
    Address of right child passed in.
    Re-initialize root page to contain the new root node.
    New root node points to two children.
    */
    InternalNode root = pager.get_page(root_page_num);
    Node right_child = pager.get_page(right_child_page_num);
    uint32_t left_child_page_num = pager.get_unused_page_num();
    Node left_child = pager.get_page(left_child_page_num);

    /* Left child has data copied from old root */
    memcpy(left_child.get_node(), root.get_node(), PAGE_SIZE);
    left_child.set_node_root(false);

    /* Root node is a new internal node with one key and two children */
    root.initialize_internal_node();
    root.set_node_root(true);
    *root.internal_node_num_keys() = 1;
    *root.internal_node_child(0) = left_child_page_num;
    uint32_t left_child_max_key = left_child.get_node_max_key();
    *root.internal_node_key(0) = left_child_max_key;
    *root.internal_node_right_child() = right_child_page_num;

    *left_child.node_parent() = root_page_num;
    *right_child.node_parent() = root_page_num;
}

Table::~Table()
{
    for (uint32_t i = 0; i < pager.num_pages; i++)
    {
        if (pager.pages[i] == nullptr)
        {
            continue;
        }
        pager.pager_flush(i);
        free(pager.pages[i]);
        pager.pages[i] = nullptr;
    }

    int result = close(pager.file_descriptor);
    if (result == -1)
    {
        std::cout << "Error closing db file." << std::endl;
        exit(EXIT_FAILURE);
    }
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        void *page = pager.pages[i];
        if (page)
        {
            free(page);
            pager.pages[i] = nullptr;
        }
    }
}



class Statement
{
public:
    StatementType type;
    Row row_to_insert;
};

class DB
{
private:
    Table *table;

public:
    DB(const char *filename)
    {
        table = new Table(filename);
    }
    
    void start();
    void print_prompt();

    bool parse_meta_command(std::string &command);
    MetaCommandResult do_meta_command(std::string &command);

    PrepareResult prepare_insert(std::string &input_line, Statement &statement);
    PrepareResult prepare_statement(std::string &input_line, Statement &statement);
    bool parse_statement(std::string &input_line, Statement &statement);
    
    void execute_statement(Statement &statement);
    ExecuteResult execute_insert(Statement &statement);
    ExecuteResult execute_select(Statement &statement);

    ~DB()
    {
        delete table;
    }
};

void DB::print_prompt()
{
    std::cout << "db > ";
}

// 元语句判断 和 执行
bool DB::parse_meta_command(std::string &command)
{
    if (command[0] == '.')
    {
        switch (do_meta_command(command))
        {
        case META_COMMAND_SUCCESS:
            return true;
        case META_COMMAND_UNRECOGNIZED_COMMAND:
            std::cout << "Unrecognized command: " << command << std::endl;
            return true;
        }
    }
    return false;
}
MetaCommandResult DB::do_meta_command(std::string &command)
{
    if (command == ".exit")
    {
        delete (table);
        std::cout << "Bye!" << std::endl;
        exit(EXIT_SUCCESS);
    }
    else if (command == ".btree")
    {
        std::cout << "Tree:" << std::endl;
        Node root_node = table->pager.get_page(table->root_page_num);
        table->pager.print_tree(0, 0);
        return META_COMMAND_SUCCESS;
    }
    else if (command == ".constants")
    {
        std::cout << "Constants:" << std::endl;
        std::cout << "ROW_SIZE: " << ROW_SIZE << std::endl;
        std::cout << "COMMON_NODE_HEADER_SIZE: " << COMMON_NODE_HEADER_SIZE << std::endl;
        std::cout << "LEAF_NODE_HEADER_SIZE: " << LEAF_NODE_HEADER_SIZE << std::endl;
        std::cout << "LEAF_NODE_CELL_SIZE: " << LEAF_NODE_CELL_SIZE << std::endl;
        std::cout << "LEAF_NODE_SPACE_FOR_CELLS: " << LEAF_NODE_SPACE_FOR_CELLS << std::endl;
        std::cout << "LEAF_NODE_MAX_CELLS: " << LEAF_NODE_MAX_CELLS << std::endl;
        return META_COMMAND_SUCCESS;
    }
    else
    {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

// 解析状态
bool DB::parse_statement(std::string &input_line, Statement &statement)
{
    switch (prepare_statement(input_line, statement))
    {
    case PREPARE_SUCCESS:
        return false;
    case (PREPARE_NEGATIVE_ID):
        std::cout << "ID must be positive." << std::endl;
        return true;
    case (PREPARE_STRING_TOO_LONG):
        std::cout << "String is too long." << std::endl;
        return true;
    case PREPARE_SYNTAX_ERROR:
        std::cout << "Syntax error. Could not parse statement." << std::endl;
        return true;
    case PREPARE_UNRECOGNIZED_STATEMENT:
        std::cout << "Unrecognized keyword at start of '" << input_line << "'." << std::endl;
        return true;
    }
    return false;
}
// 准备状态，尤其是inset语句
PrepareResult DB::prepare_statement(std::string &input_line, Statement &statement)
{
    if (!input_line.compare(0, 6, "insert"))
    {
        return prepare_insert(input_line, statement);
    }
    else if (!input_line.compare(0, 6, "select"))
    {
        statement.type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    else
    {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }
}

// insert需要准备数据
PrepareResult DB::prepare_insert(std::string &input_line, Statement &statement)
{
    // type
    statement.type = STATEMENT_INSERT;
    // row
    char *insert_line = (char *)input_line.c_str();// c_str
    char *keyword = strtok(insert_line, " ");
    char *id_string = strtok(NULL, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");
    if (id_string == NULL || username == NULL || email == NULL)
    {
        return PREPARE_SYNTAX_ERROR;
    }
    int id = atoi(id_string);
    if (id < 0)
    {
        return PREPARE_NEGATIVE_ID;
    }
    if (strlen(username) > COLUMN_USERNAME_SIZE)
    {
        return PREPARE_STRING_TOO_LONG;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE)
    {
        return PREPARE_STRING_TOO_LONG;
    }
    statement.row_to_insert = Row(id, username, email);
    return PREPARE_SUCCESS;
}

// 根据Statement type 执行语句
void DB::execute_statement(Statement &statement)
{
    ExecuteResult result;
    switch (statement.type)
    {
    case STATEMENT_INSERT:
        result = execute_insert(statement);
        break;
    case STATEMENT_SELECT:
        result = execute_select(statement);
        break;
    }

    switch (result)
    {
    case EXECUTE_SUCCESS:
        std::cout << "Executed." << std::endl;
        break;
    case (EXECUTE_DUPLICATE_KEY):
        std::cout << "Error: Duplicate key." << std::endl;
        break;
    case EXECUTE_TABLE_FULL:
        std::cout << "Error: Table full." << std::endl;
        break;
    }
}
// ***执行语句
ExecuteResult DB::execute_insert(Statement &statement)
{
    // root_page_num得到？？
    LeafNode leaf_node = table->pager.get_page(table->root_page_num);
    uint32_t num_cells = *leaf_node.leaf_node_num_cells();
    // table_find 得到光标位置
    Cursor *cursor = table->table_find(statement.row_to_insert.id);
    if (cursor->cell_num < num_cells)
    {
        uint32_t key_at_index = *leaf_node.leaf_node_key(cursor->cell_num);
        if (key_at_index == statement.row_to_insert.id)
        {
            return EXECUTE_DUPLICATE_KEY;
        }
    }
    // 根据cursor指向的页码插入
    cursor->leaf_node_insert(statement.row_to_insert.id, statement.row_to_insert);
    delete cursor;
    return EXECUTE_SUCCESS;
}
ExecuteResult DB::execute_select(Statement &statement)
{
    // start of the table
    Cursor *cursor = new Cursor(table);
    Row row;
    while (!cursor->end_of_table)
    {
        deserialize_row(cursor->cursor_value(), row);
        std::cout << "(" << row.id << ", " << row.username << ", " << row.email << ")" << std::endl;
        cursor->cursor_advance();
    }
    delete cursor;
    return EXECUTE_SUCCESS;
}


void DB::start()
{
    while (true)
    {
        print_prompt();// 1.输入行提示
        std::string input_line;// 2.getline得到字符串语句
        std::getline(std::cin, input_line);
        if (parse_meta_command(input_line)) // 3.元语句判断和执行 true时下个循环
        {
            continue;
        }
        Statement statement;
        if (parse_statement(input_line, statement))// 4.解析语句返回状态
        {
            continue;
        }
        execute_statement(statement);   
    }
}

int main(int argc, char const *argv[])
{
    if (argc < 2)
    {
        std::cout << "Must supply a database filename." << std::endl;
        exit(EXIT_FAILURE);
    }
    DB db(argv[1]);
    db.start();
    return 0;
}
