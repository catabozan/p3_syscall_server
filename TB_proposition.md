# TB System call server

## Contexte et description du projet

L'objectif de ce projet est de développer un **serveur d'execution des appels système Linux avec contrôle de permissions** capable de recevoir des requêtes d'appels système, d'évaluer ces requêtes selon des politiques de sécurité configurables, et de décider en temps réel d'autoriser ou refuser les opérations demandées.

## Architecture technique

Le système s'articule autour d'un **serveur central** composé de :
- Serveur de communication (gestion des connexions clients)
- Moteur d'évaluation de permissions (règles configurables)
- Système de logging

**Client de test** : Une bibliothèque cliente simple sera développée pour tester et valider les capacités du serveur. Ce composant est secondaire et sert uniquement à démontrer le fonctionnement du serveur.

## Objectifs du projet

Le travail comprendra plusieurs axes principaux :

**Recherche et conception**
- Étude des appels système a implementer.
- Étude des architectures de serveurs de politiques (Policy Decision Point)
- Conception de l'architecture (performance, scalabilité, sécurité)
- Définition du protocole de communication et format des politiques

**Développement du serveur**
- Implémentation du serveur capable de gérer des connexions simultanées
- Système de règles flexibles (filtrage par appel, arguments, contexte, quotas)
- Profils de sécurité prédéfinis pour cas d'usage courants
- Développement d'un client de test simple

**Administration et déploiement**
- Outils en ligne de commande
- Tests de charge et benchmarks de performance
- Documentation complète

## Technologies et compétences

- Programmation système Linux et compréhension des appels système
- Programmation réseau
- Principes de sécurité et contrôle d'accès
  
## Livrables attendus

- Code source complet et documenté du serveur
- Client de test pour validation
- Tests et éventuels benchmarks de performance
- Documentation technique, guide de déploiement
- Exemples de profils de sécurité
- Rapport final
