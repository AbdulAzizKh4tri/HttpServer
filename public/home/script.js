console.log("loaded");
const url = "http://localhost:8080/tests/debug/request";
fetch(url).then((res) => res.json()).then((data) => console.log(data));
