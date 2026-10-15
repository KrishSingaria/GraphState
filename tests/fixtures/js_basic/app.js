const path = require('path');
const { format } = require('./formatter');

class DataHandler {
  constructor(tag) {
    this.tag = tag;
  }

  process(data) {
    return format(data);
  }
}

function processAll(items) {
  const handler = new DataHandler("main");
  for (const item of items) {
    handler.process(item);
  }
}

const calculateScore = (base, bonus = 0) => {
  return base + bonus;
};

module.exports = {
  DataHandler,
  processAll,
  calculateScore
};
