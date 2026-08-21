.PHONY: all s905x-miner test benchmark clean webapp-build webapp-lint

all:
	$(MAKE) -C s905x-miner all

test:
	$(MAKE) -C s905x-miner test

benchmark:
	$(MAKE) -C s905x-miner benchmark

clean:
	$(MAKE) -C s905x-miner clean
	rm -rf dist

webapp-build:
	npm run build

webapp-lint:
	npm run lint
