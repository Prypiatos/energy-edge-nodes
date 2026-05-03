import paho.mqtt.client as mqtt
from kafka import KafkaProducer
import json

MQTT_BROKER  = "localhost"       # your MQTT broker IP
MQTT_PORT    = 1883
KAFKA_BROKER = "localhost:9092"  # your Kafka broker IP

producer = KafkaProducer(
    bootstrap_servers=KAFKA_BROKER,
    value_serializer=lambda v: json.dumps(v).encode('utf-8')
)

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = json.loads(msg.payload.decode())

    # map MQTT topic → Kafka topic
    if "telemetry" in topic:
        kafka_topic = "energy.telemetry"
    elif "health" in topic:
        kafka_topic = "energy.health"
    elif "events" in topic:
        kafka_topic = "energy.events"
    else:
        return

    producer.send(kafka_topic, payload)
    print(f"Forwarded {topic} → {kafka_topic}")

client = mqtt.Client()
client.on_message = on_message
client.connect(MQTT_BROKER, MQTT_PORT)
client.subscribe("energy/nodes/#")   # subscribes to ALL your nodes
client.loop_forever()                 # runs forever