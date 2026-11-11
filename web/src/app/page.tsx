import { SystemStatus } from "@/components/SystemStatus";
import { LeaderboardTable } from "@/components/LeaderboardTable";

export default function Home() {
  return (
    <div className="space-y-6">
      <SystemStatus />
      <LeaderboardTable />
    </div>
  );
}
