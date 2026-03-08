# Project Workflow Guide

This repository uses a **transaction-based workflow** to integrate creative design work, documentation updates, and implementation tasks.

The goal is to allow **creative exploration outside the repository** (for example in ChatGPT conversations) while keeping the repository organized, auditable, and easy for agents like Codex to process.

---

# High-Level Workflow

The project development pipeline works like this:

Creative Design (ChatGPT or brainstorming)
                ↓
        Create Transaction
                ↓
     docs/transactions.md
                ↓
Agent or User Applies Changes
                ↓
 docs/transaction_history.md

This allows the project to maintain a clear **history of design decisions and implementations**.

---

# Key Files

## docs/transactions.md

This file contains **pending transactions** that need to be applied to the repository.

Each transaction describes:

- what change should be made
- which files should be modified
- what content should be added or updated

Example structure:

TRANSACTION: TX-0003

STATUS: PENDING  
CREATED: 2026-03-08T19:30:00Z  
COMPLETED: null  

AUTHOR: user  

DESCRIPTION:  
Add canal leak simulation notes.

TARGET FILES:  
- docs/simulation_model.md  

CHANGES:  
Describe how canal cracks allow water seepage and downstream swamp formation.

Transactions in this file represent **work waiting to be integrated**.

---

## docs/transaction_history.md

This file contains **completed transactions**.

Once a transaction is implemented, it is moved from:

docs/transactions.md

to

docs/transaction_history.md

Completed transactions include:

- completion timestamp
- executor (user or agent)

Example:

STATUS: COMPLETE  
COMPLETED: 2026-03-08T19:05:00Z  
EXECUTED_BY: user

This file serves as the **permanent record of design and implementation changes**.

---

# Transaction Status Values

Transactions may have the following states:

### PENDING
The transaction has not been applied yet.

### IN_PROGRESS
An agent or user is currently applying the change.

### COMPLETE
The change has been successfully implemented.

### REJECTED
The change was reviewed but intentionally not applied.

---

# Who Creates Transactions

Transactions are typically created when:

- new design ideas are finalized
- documentation changes are needed
- architectural updates are required
- gameplay systems are defined
- code tasks are identified

These often originate from:

- ChatGPT design sessions
- personal brainstorming
- system design discussions

---

# Who Executes Transactions

Transactions may be executed by:

- the **user manually**
- **Codex agents**
- other automation tools

When completed, transactions should include:

EXECUTED_BY: user

or

EXECUTED_BY: codex

---

# Processing Rules

Transactions should be processed:

1. **Sequentially**
2. **From oldest to newest**
3. **Only modifying files listed in TARGET FILES**

After completion:

1. Update STATUS to COMPLETE
2. Add a COMPLETED timestamp
3. Move the transaction to docs/transaction_history.md

---

# Why This System Exists

This system solves several problems:

### Creative Work Happens Outside the Repo
Ideas often come from brainstorming sessions or ChatGPT conversations.

### Implementation Happens Inside the Repo
Codex and other agents need structured instructions.

### Transactions Bridge the Gap

They allow ideas to move from **creative space → structured repository updates** safely.

---

# Benefits

This workflow provides:

- clear change tracking
- reproducible updates
- safe automation for AI agents
- clean documentation history
- minimal copy/paste chaos
- separation of brainstorming and implementation

---

# Current Transaction Queue

Transactions awaiting implementation are listed in:

docs/transactions.md

If this file currently contains:

Currently no pending transactions.

then the repository is **up to date**.

---

# Design Philosophy Reminder

This project focuses on:

- world-first design
- layered simulation systems
- ancient infrastructure
- modern adaptations
- cascading consequences
- player discovery through interaction

Transactions should preserve and strengthen these core ideas.

---

# Future Improvements (Optional)

Possible upgrades to the workflow include:

- proposals.md for raw brainstorming
- automated transaction processing
- dependency tracking between transactions
- rollback support for experimental features