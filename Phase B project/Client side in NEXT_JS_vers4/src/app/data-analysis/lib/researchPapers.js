export const RESEARCH_PAPERS = [
  {
    id: 1,
    shortTitle: "Multi-Sensor Fusion",
    title: "Multi-sensor fusion based robust row following for compact agricultural robots",
    citation:
      "Velasquez, A. E. B., Higuti, V. A. H., Gasparino, M. V., Sivakumar, A. N. V., Becker, M., & Chowdhary, G. (2022). Multi-sensor fusion based robust row following for compact agricultural robots. Field Robotics, 2, 1291–1319.",
    topic: "Robot localization · LiDAR · row following",
    mainIdea:
      "Uses LiDAR, IMU, and sensor fusion to help small agricultural robots navigate crop rows where GPS is unreliable.",
    relevance:
      "Supports the way EcoFarm can connect every sensor reading or tomato detection to a location inside greenhouse rows.",
    systemComponent: "Robot position and row linkage",
    courseConnection:
      "Turns robot movement into spatial data, which is required before autocorrelation or Kriging can be meaningful.",
    keywords: ["lidar", "imu", "fusion", "localization", "robot", "row", "navigation", "spatial", "map"],
  },
  {
    id: 2,
    shortTitle: "YOLO Tomato Detection",
    title: "Evaluating the Single-Shot MultiBox Detector and YOLO Deep Learning Models for the Detection of Tomatoes in a Greenhouse",
    citation:
      "Magalhães, S. A., Castro, L., Moreira, G., dos Santos, F. N., Cunha, M., Dias, J., & Moreira, A. P. (2021). Evaluating the Single-Shot MultiBox Detector and YOLO Deep Learning Models for the Detection of Tomatoes in a Greenhouse. Sensors, 21(10), 3569.",
    topic: "YOLO · tomato detection · maturity layer",
    mainIdea:
      "Compares deep-learning models for detecting tomatoes in greenhouse images and supports using YOLO-style object detection.",
    relevance:
      "Justifies the planned YOLO12M layer that identifies tomato clusters, counts tomatoes, and classifies maturity state.",
    systemComponent: "Tomato maturity and object detection layer",
    courseConnection:
      "Provides the biological response layer that can later be analyzed spatially and over time.",
    keywords: ["yolo", "tomato", "detection", "greenhouse", "maturity", "ripe", "unripe", "vision", "class"],
  },
  {
    id: 3,
    shortTitle: "Microclimate Measurement",
    title: "On the measurement of microclimate",
    citation:
      "Maclean, I. M., Duffy, J. P., Haesen, S., Govaert, S., De Frenne, P., Vanneste, T., et al. (2021). On the measurement of microclimate. Methods in Ecology and Evolution, 12(8), 1397–1410.",
    topic: "Microclimate · temperature · humidity",
    mainIdea:
      "Explains how fine-scale environmental measurements near plants can be affected by local conditions and measurement errors.",
    relevance:
      "Supports the M5Stick layer: temperature, humidity, pressure, and gas resistance are treated as local greenhouse microclimate indicators.",
    systemComponent: "Environmental monitoring layer",
    courseConnection:
      "Connects abiotic measurements to ecological modeling of the greenhouse environment.",
    keywords: ["microclimate", "temperature", "humidity", "environment", "sensor", "pressure", "gas", "abiotic"],
  },
  {
    id: 4,
    shortTitle: "4D Crop Monitoring",
    title: "4D crop monitoring: Spatio-temporal reconstruction for agriculture",
    citation:
      "Dong, J., Burnham, J. G., Boots, B., Rains, G., & Dellaert, F. (2017). 4D crop monitoring: Spatio-temporal reconstruction for agriculture. In 2017 IEEE International Conference on Robotics and Automation (ICRA), 3878–3885.",
    topic: "Spatio-temporal crop monitoring",
    mainIdea:
      "Presents crop monitoring as a combination of spatial reconstruction and repeated measurements over time.",
    relevance:
      "Supports EcoFarm's timeline approach: each scan updates the known greenhouse map and creates a historical maturity log.",
    systemComponent: "Historical map and temporal trend layer",
    courseConnection:
      "Links crop growth, time, and spatial reconstruction into one ecological monitoring model.",
    keywords: ["4d", "spatio", "temporal", "time", "history", "crop", "monitoring", "reconstruction", "trend"],
  },
  {
    id: 5,
    shortTitle: "External Variables / GIS",
    title: "The role of external variables and GIS databases in geostatistical analysis",
    citation:
      "Pebesma, E. J. (2006). The role of external variables and GIS databases in geostatistical analysis. Transactions in GIS, 10(4), 615–632.",
    topic: "Geostatistics · Kriging · external variables",
    mainIdea:
      "Explains how external spatial variables can improve geostatistical prediction and mapping.",
    relevance:
      "Supports using Kriging and spatial variables to estimate unsampled greenhouse cells and show prediction uncertainty.",
    systemComponent: "Kriging, spatial autocorrelation, and uncertainty layer",
    courseConnection:
      "Provides the direct statistical basis for variograms, spatial prediction, and unsampled-location estimation.",
    keywords: ["kriging", "gis", "geostatistical", "variogram", "uncertainty", "prediction", "spatial", "autocorrelation"],
  },
];

const ANSWER_TEMPLATES = {
  kriging:
    "For EcoFarm, Kriging uses the maturity values already observed by the robot to estimate maturity in greenhouse locations that were not directly scanned. This is useful only if the tomato maturity layer has spatial structure, meaning nearby clusters tend to be more similar than distant clusters.",
  yolo:
    "The YOLO source supports the tomato maturity layer. In the project, YOLO12M detections become ecological observations: tomato class, confidence, count, and cluster location.",
  microclimate:
    "The microclimate source supports the environmental layer. M5Stick measurements such as temperature, humidity, pressure, and gas resistance represent local abiotic conditions around the plants.",
  lidar:
    "The LiDAR and sensor-fusion source supports spatial positioning. The robot must link each environmental sample and tomato detection to a location in the greenhouse before the dashboard can build a spatial model.",
  timeline:
    "The 4D crop-monitoring source supports the time dimension. EcoFarm is not only a static map; repeated scans create a history of how tomato maturity and environmental conditions change over time.",
  default:
    "The most relevant papers are selected by matching the question to the local article summaries. The answer below connects the papers to EcoFarm's ecological model, spatial map, microclimate layer, tomato maturity layer, and Kriging-based prediction.",
};

export function searchResearchPapers(query) {
  const clean = String(query ?? "").trim().toLowerCase();
  if (!clean) return RESEARCH_PAPERS;

  const terms = clean.split(/\s+/).filter(Boolean);

  return RESEARCH_PAPERS
    .map((paper) => {
      const haystack = [
        paper.shortTitle,
        paper.title,
        paper.topic,
        paper.mainIdea,
        paper.relevance,
        paper.systemComponent,
        paper.courseConnection,
        paper.keywords.join(" "),
      ]
        .join(" ")
        .toLowerCase();

      const score = terms.reduce((sum, term) => {
        if (haystack.includes(term)) return sum + 2;
        if (paper.keywords.some((keyword) => keyword.includes(term) || term.includes(keyword))) return sum + 3;
        return sum;
      }, 0);

      return { paper, score };
    })
    .filter((item) => item.score > 0)
    .sort((a, b) => b.score - a.score || a.paper.id - b.paper.id)
    .map((item) => item.paper);
}

export function buildRagAnswer(question) {
  const clean = String(question ?? "").trim();
  const lower = clean.toLowerCase();
  const sources = searchResearchPapers(clean).slice(0, 3);

  let template = ANSWER_TEMPLATES.default;
  if (/kriging|variogram|gis|unsampled|prediction|predict|autocorrelation/.test(lower)) template = ANSWER_TEMPLATES.kriging;
  else if (/yolo|tomato|maturity|ripe|class|detect/.test(lower)) template = ANSWER_TEMPLATES.yolo;
  else if (/microclimate|temperature|humidity|pressure|gas|sensor|environment/.test(lower)) template = ANSWER_TEMPLATES.microclimate;
  else if (/lidar|location|position|row|robot|map/.test(lower)) template = ANSWER_TEMPLATES.lidar;
  else if (/time|timeline|history|trend|4d|temporal/.test(lower)) template = ANSWER_TEMPLATES.timeline;

  const selectedSources = sources.length ? sources : RESEARCH_PAPERS.slice(0, 3);
  const sourceText = selectedSources.map((paper) => `[${paper.id}] ${paper.shortTitle}`).join(", ");
  const details = selectedSources
    .map((paper) => `${paper.shortTitle}: ${paper.relevance}`)
    .join(" ");

  return {
    answer: `${template} Relevant sources: ${sourceText}. ${details}`,
    sources: selectedSources,
  };
}
