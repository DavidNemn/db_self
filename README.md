# 如何用C++实现一个简易数据库
*基于cstack/db_tutorial C语言版本*
* KCNyu
* 2022/2/2

作为笔者写的第一个系列型教程，还是选择基于前人的教程经验以及添加一些自己个人的探索。也许有很多纰漏之处，希望大家指正。

## 1. 数据库是什么？
数据库是“按照数据结构来组织、存储和管理数据的仓库”。是一个长期存储在计算机内的、有组织的、可共享的、统一管理的大量数据的集合。使用者可以对其中的资料执行新增、选取、更新、刪除等操作。

## 2. SQL是什么？
SQL ***(Structured Query Language:结构化查询语言)*** 是一种特定目的程式语言，用于管理关系数据库管理系统 ***(RDBMS)***，或在关系流数据管理系统 ***(RDSMS)*** 中进行流处理。

## 3. 我们最终会实现什么？
前端 ***(front-end)***

I、分词器 ***(tokenizer)***

II、解析器 ***(parser)***

III、代码生成器 ***(code generator)***

后端 ***(back-end)***

I、虚拟机 ***(virtual machine)***

II、B树 ***(B-tree)***

III、分页 ***(pager)***

IV、操作系统层接口 ***(os interface)***

经过本教程，大家将一起用 ***C++*** 实现一个类似于sqlite的关系型数据库。同时，本教程将基于原作者教程章节划分。此外所有的实现按章节独立且均基于 ***db.cpp*** 一个文件，方便读者学习。

## 4. 教学大纲
* [tutorial01-实现REPL](./tutorial01/README.md)
* [tutorial02-实现解析前端和虚拟机](./tutorial02/README.md)
* [tutorial03-实现insert和select](./tutorial03/README.md)
* [tutorial04-测试与修复BUG](./tutorial04/README.md)