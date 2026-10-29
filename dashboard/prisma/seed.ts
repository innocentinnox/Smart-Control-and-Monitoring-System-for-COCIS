import "dotenv/config";
import { prisma } from "../lib/db";

/** Registers the entrance hub. The secret must match the one the hub signs with. */
async function main() {
  const secret = process.env.DEVICE_SECRET;
  if (!secret || secret.length < 32) {
    throw new Error("DEVICE_SECRET must be set (at least 32 characters), see .env.example");
  }
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
