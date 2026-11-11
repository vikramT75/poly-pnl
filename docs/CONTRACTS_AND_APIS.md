# Contracts, Events, and APIs — Verified Reference

**Status:** VERIFIED — populated from Phase 0 research (2026-11-11)  
**Sources:** github.com/Polymarket, polygonscan.com, docs.polymarket.com, docs.envio.dev

> [!IMPORTANT]
> **CLOB V2 Upgrade (April 28, 2026 — ~Polygon block 86,126,998)**  
> Polymarket completed a full exchange upgrade. Both exchange contract addresses changed, the `OrderFilled` event signature gained 2 new fields, and the collateral token changed from USDC.e → pUSD.  
> The CTF (Gnosis) contract and NegRisk Adapter addresses **did NOT change**.  
> The decoder must handle BOTH V1 and V2 `OrderFilled` signatures.

---

## 1. Contract Addresses (Polygon Mainnet, Chain ID: 137)

### V1 — Historical (pre block ~86,126,998)

| Contract | Address | Polygonscan |
|---|---|---|
| CTF Exchange V1 | `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E` | https://polygonscan.com/address/0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E |
| NegRisk CTF Exchange V1 | `0xC5d563A36AE78145C45a50134d48A1215220f80a` | https://polygonscan.com/address/0xC5d563A36AE78145C45a50134d48A1215220f80a |
| Conditional Tokens (CTF / ERC-1155) | `0x4D97DCd97eC945f40cF65F87097ACe5EA0476045` | https://polygonscan.com/address/0x4D97DCd97eC945f40cF65F87097ACe5EA0476045 |
| NegRisk Adapter | `0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296` | https://polygonscan.com/address/0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296 |
| Collateral — USDC.e | `0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174` | https://polygonscan.com/address/0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174 |

### V2 — Current (post block ~86,126,998)

| Contract | Address | Polygonscan |
|---|---|---|
| **CTF Exchange V2** | `0xE111180000d2663C0091e4f400237545B87B996B` | https://polygonscan.com/address/0xE111180000d2663C0091e4f400237545B87B996B |
| **NegRisk CTF Exchange V2** | `0xe2222d279d744050d28e00520010520000310F59` | https://polygonscan.com/address/0xe2222d279d744050d28e00520010520000310F59 |
| Conditional Tokens (unchanged) | `0x4D97DCd97eC945f40cF65F87097ACe5EA0476045` | (same as V1) |
| NegRisk Adapter (unchanged) | `0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296` | (same as V1) |
| pUSD Token (Proxy) | `0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB` | https://polygonscan.com/address/0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB |
| pUSD Token (Implementation) | `0xCe84E053301A82937F90ee2C2c1889cAb1db25dE` | https://polygonscan.com/address/0xCe84E053301A82937F90ee2C2c1889cAb1db25dE |
| CTF Collateral Adapter | `0xADa100874d00e3331D00F2007a9c336a65009718` | https://polygonscan.com/address/0xADa100874d00e3331D00F2007a9c336a65009718 |
| NegRisk CTF Collateral Adapter | `0xAdA200001000ef00D07553cEE7006808F895c6F1` | https://polygonscan.com/address/0xAdA200001000ef00D07553cEE7006808F895c6F1 |

---

## 2. Event Signatures, topic0 Hashes, and Field Layouts

### A. `OrderFilled` V1

**Solidity:**
```solidity
event OrderFilled(
    bytes32 indexed orderHash,
    address indexed maker,
    address indexed taker,
    uint256 makerAssetId,       // data[0] — ERC-1155 token id on maker side (0 = USDC.e collateral)
    uint256 takerAssetId,       // data[1] — ERC-1155 token id on taker side (0 = USDC.e collateral)
    uint256 makerAmountFilled,  // data[2]
    uint256 takerAmountFilled,  // data[3]
    uint256 fee                 // data[4] — USDC.e atomic units (6 dec)
);
```

**Canonical signature:** `OrderFilled(bytes32,address,address,uint256,uint256,uint256,uint256,uint256)`  
**topic0:** `0xd0a08e8c493f9c94f29311604c9de1b4e8c8d4c06bd0c789af57f2d65bfec0f6`

**Direction logic:**
- If `makerAssetId == 0`: maker sent USDC → maker **bought** outcome tokens. `takerAssetId` = outcome token ID.
- If `takerAssetId == 0`: taker sent USDC → taker **bought** outcome tokens. `makerAssetId` = outcome token ID.
- Price = `makerAmountFilled / takerAmountFilled` (maker's amount / taker's amount), adjusted for which side holds tokens.

### B. `OrderFilled` V2

**Solidity:**
```solidity
event OrderFilled(
    bytes32 indexed orderHash,
    address indexed maker,
    address indexed taker,
    uint8   side,               // data[0] — 0=BUY (maker buys tokens), 1=SELL (maker sells tokens)
    uint256 tokenId,            // data[1] — single outcome token id (no more dual asset)
    uint256 makerAmountFilled,  // data[2]
    uint256 takerAmountFilled,  // data[3]
    uint256 fee,                // data[4] — pUSD atomic units (6 dec)
    bytes32 builder,            // data[5] — builder tag
    bytes32 metadata            // data[6] — arbitrary metadata
);
```

**Canonical signature:** `OrderFilled(bytes32,address,address,uint8,uint256,uint256,uint256,uint256,bytes32,bytes32)`  
**topic0:** `0xd543adfd945773f1a62f74f0ee55a5e3b9b1a28262980ba90b1a89f2ea84d8ee`

**Direction logic:**
- `side == 0 (BUY)`: maker sent collateral, received `tokenId` shares. `makerAmountFilled` = collateral paid, `takerAmountFilled` = shares received.
- `side == 1 (SELL)`: maker sent `tokenId` shares, received collateral. `makerAmountFilled` = shares sold, `takerAmountFilled` = collateral received.
- Price = collateral_amount / token_amount (normalized to 1e6).

### C. `PositionSplit`

**Solidity:**
```solidity
event PositionSplit(
    address indexed stakeholder,
    IERC20          collateralToken,     // data[0] (ABI slot 0, 32 bytes, addr left-padded)
    bytes32 indexed parentCollectionId,
    bytes32 indexed conditionId,
    uint256[]       partition,           // data[1] dynamic array offset, then data
    uint256         amount               // last static slot — collateral split
);
```

**topic0:** `0x633dad6e82813589eb1765cd6c174f8841daea2913e2bb7a875880fcd07fb5e5`

### D. `PositionsMerge`

Same field layout as `PositionSplit`.  
**topic0:** `0x6f13ca62553fcc2bcd2372180a43949c1e4cebba603901ede2f4e14f36b282ca`

### E. `PayoutRedemption`

> [!NOTE]
> `collateralToken` is **indexed** here (unlike Split/Merge where it is non-indexed).

**Solidity:**
```solidity
event PayoutRedemption(
    address indexed redeemer,
    IERC20  indexed collateralToken,     // topics[2]
    bytes32 indexed parentCollectionId,  // topics[3]
    bytes32         conditionId,         // data[0]
    uint256[]       indexSets,           // data[1] dynamic
    uint256         payout               // collateral returned (6 dec)
);
```

**topic0:** `0x2682012a4a4f1973119f1c9b90745d1bd91fa2bab387344f044cb3586864d18d`

### F. `TransferSingle` (ERC-1155)

```solidity
event TransferSingle(address indexed _operator, address indexed _from, address indexed _to, uint256 _id, uint256 _value);
```

**topic0:** `0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62`

### G. `TransferBatch` (ERC-1155)

```solidity
event TransferBatch(address indexed _operator, address indexed _from, address indexed _to, uint256[] _ids, uint256[] _values);
```

**topic0:** `0x4a39dc06d4c0dbc64b70af90fd698a233a518aa5d07e595d983b8c0526c8f7fb`

### H. NegRisk Adapter Events

#### `PositionsConverted`
```solidity
event PositionsConverted(address indexed stakeholder, bytes32 indexed marketId, uint256 indexed indexSet, uint256 amount);
```
**topic0:** `0xb03d19dddbc72a87e735ff0ea3b57bef133ebe44e1894284916a84044deb367e`

#### `MarketPrepared`
**topic0:** `0xf059ab16d1ca60e123eab60e3c02b68faf060347c701a5d14885a8e1def7b3a8`

#### `QuestionPrepared`
**topic0:** `0xaac410f87d423a922a7b226ac68f0c2eaf5bf6d15e644ac0758c7f96e2c253f7`

---

## 3. Quick topic0 Reference Table

| Event | Contract | topic0 |
|---|---|---|
| OrderFilled (V1) | CTF Exch V1 / NegRisk V1 | `0xd0a08e8c...bfec0f6` |
| OrderFilled (V2) | CTF Exch V2 / NegRisk V2 | `0xd543adfd...84d8ee` |
| PositionSplit | CTF (Gnosis) | `0x633dad6e...07fb5e5` |
| PositionsMerge | CTF (Gnosis) | `0x6f13ca62...b282ca` |
| PayoutRedemption | CTF (Gnosis) | `0x2682012a...d18d` |
| TransferSingle | CTF ERC-1155 | `0xc3d58168...0f62` |
| TransferBatch | CTF ERC-1155 | `0x4a39dc06...f7fb` |
| PositionsConverted | NegRisk Adapter | `0xb03d19dd...367e` |
| MarketPrepared | NegRisk Adapter | `0xf059ab16...b3a8` |
| QuestionPrepared | NegRisk Adapter | `0xaac410f8...53f7` |

---

## 4. API Endpoints

### Gamma API
- **Base URL:** `https://gamma-api.polymarket.com`
- **Auth:** None (public)
- **Key endpoints:** `/markets`, `/events`, `/public-search`, `/tags`
- **Key fields:** `conditionId`, `clobTokenIds[0]` (YES), `clobTokenIds[1]` (NO)

### CLOB REST API
- **Base URL:** `https://clob.polymarket.com`
- **Key endpoints:** `/book?token_id=`, `/midpoint?token_id=`, `/trades?market=`

### CLOB WebSocket (Market Channel)
- **URL:** `wss://ws-subscriptions-clob.polymarket.com/ws/market`
- **Heartbeat:** PING every 10s, server replies PONG
- **Subscribe:** `{"assets_ids": ["TOKEN_ID"], "type": "market", "initial_dump": true}`
- **Events:** `book`, `price_change`, `last_trade_price`, `best_bid_ask`, `market_resolved`

### Data API (Validation)
- **Base URL:** `https://data-api.polymarket.com`
- **Auth:** None (public read)
- **Key endpoints:** `GET /v2/positions?user=PROXY_WALLET`, `GET /v2/trades?user=PROXY_WALLET`
- **Note:** `user` = proxy wallet address (Polymarket Safe sub-wallet), not EOA signer

### Envio HyperSync (Polygon)
- **HyperSync:** `https://polygon.hypersync.xyz` (alias: `https://137.hypersync.xyz`)
- **HyperRPC:** `https://polygon.rpc.hypersync.xyz`

---

## 5. Example Transaction Fixtures

### OrderFilled V2 — CTF Exchange
- **Tx:** `0x10455b94be059c2331dfc956ec945892576b978b7b25861bf0df47cbcc5b7be2`
- **Link:** https://polygonscan.com/tx/0x10455b94be059c2331dfc956ec945892576b978b7b25861bf0df47cbcc5b7be2

### OrderFilled V2 — NegRisk CTF Exchange
- **Tx:** `0x3f5666f58af0ea43527b138e932c0276d49863c0a52f9cce9fa475f966ff6d94`
- **Link:** https://polygonscan.com/tx/0x3f5666f58af0ea43527b138e932c0276d49863c0a52f9cce9fa475f966ff6d94

### Live event browsing
- CTF events: https://polygonscan.com/address/0x4D97DCd97eC945f40cF65F87097ACe5EA0476045#events
- CTF Exchange V2: https://polygonscan.com/address/0xE111180000d2663C0091e4f400237545B87B996B#events
