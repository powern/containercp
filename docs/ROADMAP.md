# Roadmap

## Phase 0 - Project foundation

Status: in progress

Goals:

- Debian development VM
- Git repository
- C++20 project
- CMake/Ninja build
- project documentation
- basic CLI executable

## Phase 1 - CLI foundation

Goals:

- command parser
- help output
- version command
- error handling
- clean exit codes

Commands:

- containercp --help
- containercp --version
- containercp node list

## Phase 2 - Local node model

Goals:

- introduce Node entity
- create default local node
- store node information
- prepare for future remote nodes

## Phase 3 - Users and sites

Goals:

- create ContainerCP users
- create site records
- link every site to a node
- validate domain names

## Phase 4 - Docker Compose generation

Goals:

- generate per-site docker-compose.yml
- generate per-site directory structure
- do not start containers yet

## Phase 5 - Runtime control

Goals:

- start site stack
- stop site stack
- show site status
