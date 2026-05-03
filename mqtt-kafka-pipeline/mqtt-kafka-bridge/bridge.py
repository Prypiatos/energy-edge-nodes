# import paho.mqtt.client as mqtt
# from kafka import KafkaProducer
# import json

# MQTT_BROKER  = "localhost"       # your MQTT broker IP
# MQTT_PORT    = 1883
# KAFKA_BROKER = "localhost:9092"  # your Kafka broker IP

# producer = KafkaProducer(
#     bootstrap_servers=KAFKA_BROKER,
#     value_serializer=lambda v: json.dumps(v).encode('utf-8')
# )

# def on_message(client, userdata, msg):
#     topic = msg.topic
#     payload = json.loads(msg.payload.decode())

#     # map MQTT topic → Kafka topic
#     if "telemetry" in topic:
#         kafka_topic = "energy.telemetry"
#     elif "health" in topic:
#         kafka_topic = "energy.health"
#     elif "events" in topic:
#         kafka_topic = "energy.events"
#     else:
#         return

#     producer.send(kafka_topic, payload)
#     print(f"Forwarded {topic} → {kafka_topic}")

# client = mqtt.Client()
# client.on_message = on_message
# client.connect(MQTT_BROKER, MQTT_PORT)
# client.subscribe("energy/nodes/#")   # subscribes to ALL your nodes
# client.loop_forever()                 # runs forever



import os
import json
import time
import paho.mqtt.client as mqtt
from kafka import KafkaProducer
from kafka.errors import NoBrokersAvailable

# ── Config — reads from environment variables set in docker-compose.yml ───────
MQTT_BROKER  = os.getenv("MQTT_BROKER",  "localhost")
MQTT_PORT    = int(os.getenv("MQTT_PORT", "1883"))
KAFKA_BROKER = os.getenv("KAFKA_BROKER", "localhost:9092")

# ── Kafka topic mapping ───────────────────────────────────────────────────────
# Maps MQTT topic keywords → Kafka topic names
TOPIC_MAP = {
    "telemetry": "energy.telemetry",
    "health":    "energy.health",
    "events":    "energy.events",
}

# ── Wait for Kafka to be ready (it takes a few seconds to start) ──────────────
def create_producer():
    retries = 10
    for attempt in range(retries):
        try:
            producer = KafkaProducer(
                bootstrap_servers=KAFKA_BROKER,
                value_serializer=lambda v: json.dumps(v).encode("utf-8")
            )
            print(f"[bridge] Connected to Kafka at {KAFKA_BROKER}")
            return producer
        except NoBrokersAvailable:
            print(f"[bridge] Kafka not ready yet, retrying ({attempt+1}/{retries})...")
            time.sleep(5)
    raise RuntimeError("[bridge] Could not connect to Kafka after retries")

# ── MQTT callback — fires on every incoming message ───────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[bridge] Connected to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe("energy/nodes/#")   # subscribes to ALL node topics
        print("[bridge] Subscribed to energy/nodes/#")
    else:
        print(f"[bridge] MQTT connection failed with code {rc}")

def on_message(client, userdata, msg):
    topic   = msg.topic
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except json.JSONDecodeError:
        print(f"[bridge] Could not parse JSON from {topic} — skipping")
        return

    # Find which Kafka topic to forward to
    kafka_topic = None
    for keyword, kt in TOPIC_MAP.items():
        if keyword in topic:
            kafka_topic = kt
            break

    if kafka_topic is None:
        print(f"[bridge] No Kafka mapping for topic: {topic} — skipping")
        return

    # Forward to Kafka
    producer.send(kafka_topic, payload)
    print(f"[bridge] {topic}  →  {kafka_topic}")

# ── Start everything ──────────────────────────────────────────────────────────
print("[bridge] Starting MQTT → Kafka bridge...")

producer = create_producer()

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, MQTT_PORT)
client.loop_forever()   # blocks forever, handles reconnects automatically