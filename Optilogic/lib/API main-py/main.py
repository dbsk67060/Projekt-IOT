import os
import time
from datetime import datetime, timedelta

import asyncpg
from fastapi import FastAPI
from fastapi.responses import JSONResponse

# Force UTC inside container
os.environ["TZ"] = "UTC"
time.tzset()

app = FastAPI()

# --- QuestDB config ---
QDB_HOST = os.getenv("QDB_HOST", "172.18.0.4")
QDB_PORT = int(os.getenv("QDB_PORT", "8812"))
QDB_USER = os.getenv("QDB_USER", "admin")
QDB_PASSWORD = os.getenv("QDB_PASSWORD", "quest")
QDB_DATABASE = os.getenv("QDB_DATABASE", "qdb")

async def get_qdb_connection():
    return await asyncpg.connect(
        host=QDB_HOST,
        port=QDB_PORT,
        user=QDB_USER,
        password=QDB_PASSWORD,
        database=QDB_DATABASE,
    )

# -----------------
# Basic test routes
# -----------------
@app.get("/")
def root():
    return {"message": "FastAPI backend for QuestDB"}

@app.get("/health")
async def health():
    try:
        conn = await get_qdb_connection()
        await conn.execute("SELECT 1")
        await conn.close()
        return {"status": "ok", "questdb": "up"}
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"status": "error", "questdb": "down", "detail": str(e)},
        )

# -----------------
# Main data endpoint
# -----------------
@app.get("/sensors")
async def query_with_asyncpg():
    conn = await get_qdb_connection()
    try:
        rows = await conn.fetch(
            """
            SELECT 
                temp        AS temperature,
                tryk        AS pressure,
                rpm         AS airflow,
                timestamp
            FROM sensor_data
            ORDER BY timestamp DESC
            LIMIT 100
            """
        )
        data = [dict(row) for row in rows]
        return {"count": len(data), "rows": data}
    finally:
        await conn.close()
# -----------------
# QuestDB version check
# -----------------
@app.get("/qdbversion")
async def qdb_version():
    conn = await get_qdb_connection()
    try:
        version = await conn.fetchval("SELECT version()")
        return {"QuestDB_version": version}
    finally:
        await conn.close()