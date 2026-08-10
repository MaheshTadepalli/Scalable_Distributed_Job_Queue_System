FROM alpine:3.20 AS build

RUN apk add --no-cache cmake g++ make
WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target job-queue -j

FROM alpine:3.20

RUN apk add --no-cache libstdc++ libgcc
RUN adduser -D -H appuser
WORKDIR /app
COPY --from=build /app/build/job-queue /usr/local/bin/job-queue
USER appuser
EXPOSE 8080

ENV JOB_QUEUE_PORT=8080
ENV JOB_QUEUE_WORKERS=4
ENV JOB_QUEUE_MAX_RETRIES=3
ENV JOB_QUEUE_RETRY_DELAY_MS=1500

CMD ["job-queue"]
