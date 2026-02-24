# ── Stage 1: build ────────────────────────────────────────────────────────────
FROM golang:1.23-alpine AS builder

WORKDIR /app

# Download deps in a separate layer so they are cached independently of source.
COPY go.mod go.sum ./
RUN go mod download

COPY . .
# CGO_ENABLED=0 produces a fully static binary (lib/pq is pure Go).
# -s -w strips the symbol table and DWARF info to shrink the image.
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o referee .

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM alpine:3.21

# ca-certificates is needed if the service ever makes outbound TLS calls.
RUN apk add --no-cache ca-certificates

WORKDIR /app
COPY --from=builder /app/referee .

EXPOSE 8080
CMD ["./referee"]
