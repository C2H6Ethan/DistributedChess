# Distributed Chess

A fault-tolerant, highly available multiplayer chess engine built using a microservice architecture. 

This system prioritizes statelessness, seamless failover, and strict domain-driven design. It separates the User Experience (Optimistic UI via React), the System Authority (Stateless Go API), and the Pure Domain Logic (C++ Engine) into a resilient, load-balanced Docker container environment.

## 🏗 System Architecture

The infrastructure routes traffic through a Traefik API Gateway to an array of stateless Go servers, which in turn manage high-throughput, internal HTTP connection pools to a cluster of optimized C++ engines. 

For the complete visual topology and component breakdown, see the [Architecture Blueprint](ARCHITECTURE.md).

## 🚀 Tech Stack

* **Frontend:** React + `chess.js` (Optimistic UI & State Polling)
* **API Gateway:** Traefik (Edge Routing & Round-Robin Load Balancing)
* **API / Referee:** Go (Stateless JWT Authentication, Database I/O)
* **Compute Engine:** C++26 + `cpp-httplib` (High-Performance Domain Logic Microservice)
* **Database:** PostgreSQL (Persistent Game State & User Credentials)
* **Deployment:** Docker & Docker Compose

## ⚡ Quick Start (Single-Command Deployment)

The system is fully containerized and designed to run out-of-the-box. Ensure Docker and Docker Compose are installed on your machine.
```bash
docker-compose up --build -d
```
Access the application:
   * **Web UI:** `http://localhost:80`
   * **Traefik Dashboard (Traffic Monitoring):** `http://localhost:8080`

## 🛡️ Security & Authentication

This system employs a "Zero Trust" model regarding the client state:
* All move requests (`POST /api/game/move`) must be accompanied by a stateless JWT (`Bearer <token>`).
* The Go backend validates the token in-memory and enforces strict authorization checks against the PostgreSQL database before validating the move.
* The frontend state is strictly a visual representation; the C++ backend is the absolute mathematical authority on the resulting FEN strings and game terminations.
