<!--
* SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
* SPDX-License-Identifier: MIT
-->

# TIMPANI

This repository contains three components:

## Components

### [Sample Applications](sample-apps/README.md)
Real-time sample applications for real-time system analysis. Provides periodic execution, deadline monitoring, and runtime statistics collection capabilities.

- [Korean Documentation](sample-apps/README_kr.md)

### [TIMPANI-N (Time Trigger)](timpani-n/README.md)
Time Trigger component.

- [CentOS Setup](timpani-n/README.CentOS.md)
- [Ubuntu 20 Setup](timpani-n/README.Ubuntu20.md)

### [TIMPANI-O](timpani-o/README.md)
TIMPANI-O component with gRPC & protobuf support.

### [TIMPANI Rust](timpani_rust/README.md)
Rust port of the TIMPANI-O global scheduler.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 📖 Documentation Structure

```
TIMPANI/
├── README.md                    # This file - main project overview
├── sample-apps/
│   ├── README.md               # Sample applications documentation
│   └── README_kr.md           # Korean documentation
├── timpani-n/
│   ├── README.md               # Time trigger component
│   ├── README.CentOS.md       # CentOS setup guide
│   └── README.Ubuntu20.md     # Ubuntu setup guide
├── timpani-o/
│   └── README.md               # Orchestrator component
└── timpani_rust/
    └── README.md               # Rust port of the global scheduler
```



---

**Navigation:** [Sample Apps](sample-apps/) | [TIMPANI-N](timpani-n/) | [TIMPANI-O](timpani-o/) | [Rust](timpani_rust/README.md)
