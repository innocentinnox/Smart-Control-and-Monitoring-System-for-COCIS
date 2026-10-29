-- CreateSchema
CREATE SCHEMA IF NOT EXISTS "public";

-- CreateTable
CREATE TABLE "Device" (
    "id" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "secret" TEXT NOT NULL,
    "lastSeenAt" TIMESTAMP(3),
    "lastSeq" INTEGER,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "Device_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Event" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "type" TEXT NOT NULL,
    "data" JSONB NOT NULL,
    "hash" TEXT,
    "receivedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "Event_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "AccessEvent" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "uid" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "role" TEXT NOT NULL,
    "side" TEXT NOT NULL,
    "ok" BOOLEAN NOT NULL,
    "reason" TEXT NOT NULL,

    CONSTRAINT "AccessEvent_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "RoomEvent" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "room" INTEGER NOT NULL,
    "occupied" BOOLEAN NOT NULL,
    "daylight" BOOLEAN,
    "lux" INTEGER,

    CONSTRAINT "RoomEvent_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "LightEvent" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "room" INTEGER NOT NULL,
    "level" INTEGER NOT NULL,
    "mode" TEXT NOT NULL,
    "lux" INTEGER,

    CONSTRAINT "LightEvent_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "EnergySample" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "room" INTEGER NOT NULL,
    "onWh" DOUBLE PRECISION NOT NULL,
    "savedWh" DOUBLE PRECISION NOT NULL,

    CONSTRAINT "EnergySample_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Alert" (
    "id" SERIAL NOT NULL,
    "deviceId" TEXT NOT NULL,
    "seq" INTEGER NOT NULL,
    "ts" TIMESTAMP(3) NOT NULL,
    "code" TEXT NOT NULL,
    "detail" TEXT NOT NULL,
    "acknowledged" BOOLEAN NOT NULL DEFAULT false,

    CONSTRAINT "Alert_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE INDEX "Event_type_ts_idx" ON "Event"("type", "ts");

-- CreateIndex
CREATE UNIQUE INDEX "Event_deviceId_seq_key" ON "Event"("deviceId", "seq");

-- CreateIndex
CREATE INDEX "AccessEvent_uid_ts_idx" ON "AccessEvent"("uid", "ts");

-- CreateIndex
CREATE INDEX "AccessEvent_ts_idx" ON "AccessEvent"("ts");

-- CreateIndex
CREATE UNIQUE INDEX "AccessEvent_deviceId_seq_key" ON "AccessEvent"("deviceId", "seq");

-- CreateIndex
CREATE INDEX "RoomEvent_room_ts_idx" ON "RoomEvent"("room", "ts");

-- CreateIndex
CREATE UNIQUE INDEX "RoomEvent_deviceId_seq_key" ON "RoomEvent"("deviceId", "seq");

-- CreateIndex
CREATE INDEX "LightEvent_room_ts_idx" ON "LightEvent"("room", "ts");

-- CreateIndex
CREATE UNIQUE INDEX "LightEvent_deviceId_seq_key" ON "LightEvent"("deviceId", "seq");

-- CreateIndex
CREATE INDEX "EnergySample_room_ts_idx" ON "EnergySample"("room", "ts");

-- CreateIndex
CREATE UNIQUE INDEX "EnergySample_deviceId_seq_key" ON "EnergySample"("deviceId", "seq");

-- CreateIndex
CREATE INDEX "Alert_ts_idx" ON "Alert"("ts");

-- CreateIndex
CREATE UNIQUE INDEX "Alert_deviceId_seq_key" ON "Alert"("deviceId", "seq");

-- AddForeignKey
ALTER TABLE "Event" ADD CONSTRAINT "Event_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "AccessEvent" ADD CONSTRAINT "AccessEvent_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "RoomEvent" ADD CONSTRAINT "RoomEvent_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "LightEvent" ADD CONSTRAINT "LightEvent_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "EnergySample" ADD CONSTRAINT "EnergySample_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Alert" ADD CONSTRAINT "Alert_deviceId_fkey" FOREIGN KEY ("deviceId") REFERENCES "Device"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
