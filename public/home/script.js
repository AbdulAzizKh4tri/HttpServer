console.log("loaded");
const url = "http://localhost:8080/tests/debug/request";
fetch(url).then((res) => res.json()).then((data) => console.log(data));

const url2 = "http://localhost:8080/static/home/script.js";
fetch(url2).then((res) => res.text()).then((data) => console.log(data));
