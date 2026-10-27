import { PrismaClient } from "@prisma/client";

/** Registers the entrance hub. The secret must match the one flashed into the firmware. */
const prisma = new PrismaClient();

const secret = process.env.DEVICE_SECRET ?? "dev-secret-hub-01-change-me";

async function main() {
  const device = await prisma.device.upsert({
    where: { id: "hub-01" },
    update: { secret },
    create: { id: "hub-01", name: "CoCIS entrance hub", secret },
  });
  console.log(`seeded device ${device.id}`);
}

main()
  .catch((e) => {
    console.error(e);
    process.exit(1);
  })
  .finally(() => prisma.$disconnect());
